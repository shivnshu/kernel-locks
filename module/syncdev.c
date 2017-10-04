#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define DEVICE_NAME "syncdev"
#define PAUSE_COUNT 20


static int Major;
atomic_t  open_count;

struct data{
             long num_writes;
             long counter;
             long result;
             int lock_type;
             struct cs_handler *handler; /*generic pointer to your lock specific
                                         implementations*/
};

struct data *gdata;

// For RCU lock implementation
struct data __rcu *gblrcu_data;
spinlock_t slock;

typedef enum{
                  NONE=0,
                  SPINLOCK,


                  RWLOCK,                 /*kernel read/write lock*/
                  SEQLOCK,                /*kernel seqlock*/
                  RCU,                    /*kernel RCU*/
                  RWLOCK_CUSTOM,          /*Your custom read/write lock*/
                  RESEARCH_LOCK,          /*To improve over RCU*/
                  MAX_LOCK_TYPE

}LOCK_TYPE;

// Custom RW lock
struct customrwlock_t {
    atomic_t counter;
};

struct cs_handler{

                     union{
                              spinlock_t spin;
                              rwlock_t rwlock;
                              seqlock_t seqlock;
                              struct rcu_head rcu;
                              /*Add your custom lock type here*/
                              struct customrwlock_t customlock;
                     };


                     /*Two functions below should be called from next
                       corresponding lock implementation function, respectively.
                       Ex: read_data implementation of the locking mechanism must
                       call mustcall_read with appropriate parameters*/

                     int (*mustcall_read)(struct data *gd, char *buf); /*returns the length read*/
                     int (*mustcall_write)(struct data *gd);

                     /* These functions are implemented depending on the lock
                       type used */
                     int (*init_cs)(struct data *gd);
                     int (*read_data)(struct data *gd, char *buf);
                     int (*write_data)(struct data *gd);
                     int (*cleanup_cs)(struct data *gd);
};

/*Prototype for lock implementations*/

/*XXX NO lock*/
static int nolock_init_cs(struct data *gd);
static int nolock_cleanup_cs(struct data *gd);
static int nolock_write_data(struct data *gd);
static int nolock_read_data(struct data *gd, char *buf);

/*XXX spin lock*/
static int spinlock_init_cs(struct data *gd);
static int spinlock_cleanup_cs(struct data *gd);
static int spinlock_write_data(struct data *gd);
static int spinlock_read_data(struct data *gd, char *buf);

/*XXX RWlock*/
static int rwlock_init_cs(struct data *gd);
static int rwlock_cleanup_cs(struct data *gd);
static int rwlock_write_data(struct data *gd);
static int rwlock_read_data(struct data *gd, char *buf);

/*XXX Seqlock */
static int seqlock_init_cs(struct data *gd);
static int seqlock_cleanup_cs(struct data *gd);
static int seqlock_write_data(struct data *gd);
static int seqlock_read_data(struct data *gd, char *buf);

/*XXX RCU */
static int rcu_init_cs(struct data *gd);
static int rcu_cleanup_cs(struct data *gd);
static int rcu_write_data(struct data *gd);
static int rcu_read_data(struct data *gd, char *buf);

/*XXX Custom RW lock */
static int customlock_init_cs(struct data *gd);
static int customlock_cleanup_cs(struct data *gd);
static int customlock_write_data(struct data *gd);
static int customlock_read_data(struct data *gd, char *buf);


static  int readit(struct data *gd, char *buf)
{
        int retval;
        int pctr = PAUSE_COUNT;

        while(pctr--)   /*Spend some cpu cycles w/o performing anything useful*/
          cpu_relax();

        retval = sprintf(buf, "%ld %ld %ld", gd->num_writes, gd->counter, gd->result);

        pctr = PAUSE_COUNT;


        return retval;
}

static int writeit(struct data *gd)
{

        int pctr = PAUSE_COUNT;
        gd->counter++;

        while(pctr--)   /*Spend some cpu cycles w/o performing anything useful*/
          cpu_relax();

        gd->num_writes++;

        pctr = PAUSE_COUNT;

        while(pctr--)  /*Spend some cpu cycles w/o performing anything useful*/
          cpu_relax();

        gd->result = gd->num_writes + gd->counter;

        return 0;
}

static inline int set_cs_implementation(struct cs_handler *handler, unsigned newlock_type)
{
        if(newlock_type >= MAX_LOCK_TYPE)
               return -EINVAL;
        switch(newlock_type)
        {
            case NONE:
                         handler->init_cs = nolock_init_cs;
                         handler->cleanup_cs = nolock_cleanup_cs;
                         handler->read_data = nolock_read_data;
                         handler->write_data = nolock_write_data;
                         break;
           case SPINLOCK:
                         handler->init_cs = spinlock_init_cs;
                         handler->cleanup_cs = spinlock_cleanup_cs;
                         handler->read_data = spinlock_read_data;
                         handler->write_data = spinlock_write_data;
                         break;
           case RWLOCK:                /*kernel read/write lock*/
                         handler->init_cs = rwlock_init_cs;
                         handler->cleanup_cs = rwlock_cleanup_cs;
                         handler->read_data = rwlock_read_data;
                         handler->write_data = rwlock_write_data;
                         break;
           case SEQLOCK:               /*kernel seqlock*/
                         handler->init_cs = seqlock_init_cs;
                         handler->cleanup_cs = seqlock_cleanup_cs;
                         handler->read_data = seqlock_read_data;
                         handler->write_data = seqlock_write_data;
                         break;
           case RCU:                    /*kernel RCU*/
                         handler->init_cs = rcu_init_cs;
                         handler->cleanup_cs = rcu_cleanup_cs;
                         handler->read_data = rcu_read_data;
                         handler->write_data = rcu_write_data;
                         break;
           case RWLOCK_CUSTOM:         /*Your custom read/write lock*/
                         handler->init_cs = customlock_init_cs;
                         handler->cleanup_cs = customlock_cleanup_cs;
                         handler->read_data = customlock_read_data;
                         handler->write_data = customlock_write_data;
                         break;
           case RESEARCH_LOCK:          /*To improve over RCU*/
           default:
                    printk(KERN_INFO "Not implemented currently, you have to implement these as shown for the first two cases\n");
                    return -EINVAL;
        }
      return 0;
}

static inline int init_cs_handler(struct data *gd)
{
  struct cs_handler *handler;

  BUG_ON(!gd || gd->handler);

  handler = kzalloc(sizeof(struct cs_handler), GFP_KERNEL);
  if(!handler)
       return -ENOMEM;
  handler->mustcall_read = readit;
  handler->mustcall_write = writeit;
  if(set_cs_implementation(handler, gd->lock_type))
      return -EINVAL;
  gd->handler = handler;
  return 0;
}

static  inline int free_cs_handler(struct data *gd)
{
   kfree(gd->handler);
   return 0;
}

//////////////////////////////////////////////////////////////////////////////////
/*All lock implementations go here. On sysfs variable change, you should change
  the last four functions of the data->handler to your implementation*/
//////////////////////////////////////////////////////////////////////////////////

/*No lock implementation*/

int nolock_init_cs(struct data *gd)
{
   return 0;
}
int nolock_cleanup_cs(struct data *gd)
{
   return 0;
}

int nolock_write_data(struct data *gd)
{

  BUG_ON(!gdata->handler->mustcall_write);
  gdata->handler->mustcall_write(gd);
  return 0;
}
int nolock_read_data(struct data *gd, char *buf)
{

  BUG_ON(!gdata->handler->mustcall_read);
  gdata->handler->mustcall_read(gd, buf);
  return 0;
}

/*Spin lock implementation*/

int spinlock_init_cs(struct data *gd)
{
   struct cs_handler *handler = gd->handler;
   spin_lock_init(&handler->spin);
   return 0;
}
int spinlock_cleanup_cs(struct data *gd)
{
   return 0;
}

int spinlock_write_data(struct data *gd)
{

  struct cs_handler *handler = gd->handler;
  BUG_ON(!handler->mustcall_write);

  spin_lock(&handler->spin);
  handler->mustcall_write(gd);  /*Call the Write CS*/
  spin_unlock(&handler->spin);

  return 0;
}
int spinlock_read_data(struct data *gd, char *buf)
{
  struct cs_handler *handler = gd->handler;
  BUG_ON(!handler->mustcall_read);

  spin_lock(&handler->spin);
  handler->mustcall_read(gd, buf);  /*Call the read CS*/
  spin_unlock(&handler->spin);
  return 0;
}

/*TODO   Your implementations for assignment II*/

/* RW Lock implementation */
int rwlock_init_cs(struct data *gd)
{
    struct cs_handler *handler = gd->handler;
    rwlock_init(&handler->rwlock);
    return 0;
}

int rwlock_cleanup_cs(struct data* gd)
{
    return 0;
}

int rwlock_write_data(struct data* gd)
{
    struct cs_handler *handler = gd->handler;
    BUG_ON(!handler->mustcall_write);

    write_lock(&handler->rwlock);
    handler->mustcall_write(gd);
    write_unlock(&handler->rwlock);

    return 0;
}

int rwlock_read_data(struct data* gd,char* buf)
{
    struct cs_handler *handler = gd->handler;
    BUG_ON(!handler->mustcall_read);

    read_lock(&handler->rwlock);
    handler->mustcall_read(gd, buf);
    read_unlock(&handler->rwlock);

    return 0;
}

/* Seq Lock implementation */

int seqlock_init_cs(struct data* gd)
{
    struct cs_handler *handler = gd->handler;
    seqlock_init(&handler->seqlock);
    return 0;
}

int seqlock_cleanup_cs(struct data* gd)
{
    return 0;
}

int seqlock_write_data(struct data* gd)
{
    struct cs_handler *handler = gd->handler;
    BUG_ON(!handler->mustcall_write);

    write_seqlock(&handler->seqlock);
    handler->mustcall_write(gd);
    write_sequnlock(&handler->seqlock);

    return 0;
}

int seqlock_read_data(struct data* gd,char* buf)
{
    struct cs_handler *handler = gd->handler;
    unsigned int seq;
    BUG_ON(!handler->mustcall_read);

    do {
        seq = read_seqbegin(&handler->seqlock);
        handler->mustcall_read(gd, buf);
    } while (read_seqretry(&handler->seqlock, seq));

    return 0;
}

/* RCU Lock implementation */
int rcu_init_cs(struct data* gd)
{
    struct cs_handler *handler;
    gblrcu_data = kmalloc(sizeof(struct data), GFP_KERNEL);
    memcpy(gblrcu_data, gd, sizeof(struct data));
    handler = gblrcu_data->handler;
    init_rcu_head(&handler->rcu);
    spin_lock_init(&slock);
    return 0;
}

int rcu_cleanup_cs(struct data* gd)
{
    memcpy(gd, gblrcu_data, sizeof(struct data));
    kfree(gblrcu_data);
    return 0;
}

int rcu_write_data(struct data* gd)
{
    struct data *old_gdata, *new_gdata;
    struct cs_handler *handler;
    new_gdata = kmalloc(sizeof(struct data), GFP_KERNEL);
    spin_lock(&slock);
    old_gdata = rcu_dereference(gblrcu_data);
    memcpy(new_gdata, old_gdata, sizeof(struct data));
    handler = new_gdata->handler;
    handler->mustcall_write(new_gdata);

    rcu_assign_pointer(gblrcu_data, new_gdata);
    spin_unlock(&slock);
    synchronize_rcu();
    kfree(old_gdata);

    return 0;
}

int rcu_read_data(struct data* gd,char* buf)
{
    struct data *ld;
    struct cs_handler *handler;

    rcu_read_lock();
    ld = rcu_dereference(gblrcu_data);
    handler = ld->handler;
    handler->mustcall_read(ld, buf);
    rcu_read_unlock();

    return 0;
}

/* Custom RWlock implementation */
int customlock_init_cs(struct data* gd)
{
    struct cs_handler *handler = gd->handler;
    atomic_set(&(handler->customlock.counter), 0x01000000); //Support 0x01000000 readers
    return 0;
}

int customlock_cleanup_cs(struct data* gd)
{
    return 0;
}

int customlock_write_data(struct data *gd)
{
    struct cs_handler *handler = gd->handler;
    BUG_ON(!handler->mustcall_write);

    // Busy waiting
    while(1) {
        if (atomic_read(&(handler->customlock.counter)) == 0x00000000)
            continue;
        atomic_cmpxchg(&(handler->customlock.counter), 0x01000000, 0x00000000);
        if (atomic_read(&(handler->customlock.counter)) == 0x00000000)
            break;
    }

    handler->mustcall_write(gd);

    atomic_set(&(handler->customlock.counter), 0x01000000);
    return 0;
}

int customlock_read_data(struct data* gd,char* buf)
{
    struct cs_handler *handler = gd->handler;
    BUG_ON(!handler->mustcall_read);

    while(1) {
        if(atomic_read(&(handler->customlock.counter)) == 0x00000000)
            continue;
        atomic_dec(&(handler->customlock.counter));
        break;
    }

    handler->mustcall_read(gd, buf);

    atomic_inc(&(handler->customlock.counter));
    return 0;
}



/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static ssize_t asg2_lock_show(struct kobject *kobj, struct kobj_attribute *attr,
                        char *buf)
{

        if(!gdata || !gdata->handler)
           return -EINVAL;
        return sprintf(buf, "%d\n", gdata->lock_type);
}

static ssize_t asg2_lock_set(struct kobject *kobj,
                                   struct kobj_attribute *attr,
                                   const char *buf, size_t count)
{
        int err;
        unsigned long mode;


        if(atomic_read(&open_count)) /*Change only when no process has opened the file*/
          return -EINVAL;

        if(!gdata || !gdata->handler)
             return -EINVAL;

        err = kstrtoul(buf, 10, &mode);
        if (err)
                return -EINVAL;

        if(set_cs_implementation(gdata->handler, mode))
            return -EINVAL;

        gdata->lock_type = mode;
        return count;
}

static struct kobj_attribute lock_type_attribute = __ATTR(asg2_lock, 0644, asg2_lock_show, asg2_lock_set);

static struct attribute *lt_attrs[] = {
                    &lock_type_attribute.attr,
                    NULL,
};
static struct attribute_group lt_attr_group = {
        .attrs = lt_attrs,
};

/*
   * Called when a process tries to open the device file, like
   * "cat /dev/mycharfile"
   */
static int device_open(struct inode *inode, struct file *file)
{

        if(atomic_read(&open_count))
            return -EBUSY;

        atomic_inc(&open_count);


        BUG_ON(!gdata || !gdata->handler || !gdata->handler->init_cs);
        gdata->num_writes = 0;
        gdata->counter = 0;
        gdata->result = 0;

        gdata->handler->init_cs(gdata);


        try_module_get(THIS_MODULE);
        return 0;
}

/*
   * Called when a process closes the device file.
   */
static int device_release(struct inode *inode, struct file *file)
{


        BUG_ON(!gdata || !gdata->handler || !gdata->handler->cleanup_cs);
        gdata->handler->cleanup_cs(gdata);
        atomic_dec(&open_count);
        module_put(THIS_MODULE);
        return 0;
}

/*
   * Called when a process, which already opened the dev file, attempts to
   * read from it.
   */
static ssize_t device_read(struct file *filp,   /* see include/linux/fs.h   */
                char *buffer,   /* buffer to fill with data (vfs_records)*/
                size_t length,  /* in bytes*/
                loff_t * offset)
{
        int retval;

        BUG_ON(!gdata || !gdata->handler || !gdata->handler->read_data);
        retval = gdata->handler->read_data(gdata, buffer);

        return retval;
}

/*
    * Called when a process writes to dev file
     Three elements of gdata structure should be updated such that
     consistency among the values maintained.
    */
static ssize_t
device_write(struct file *file, const char *buff, size_t len, loff_t * off)
{
        BUG_ON(!gdata || !gdata->handler || !gdata->handler->write_data);
        gdata->handler->write_data(gdata);

        return 0;
}
static struct file_operations fops = {
        .read = device_read,
        .write = device_write,
        .open = device_open,
        .release = device_release
};

int init_module(void)
{
        Major = register_chrdev(0, DEVICE_NAME, &fops);

        if (Major < 0) {
                printk(KERN_ALERT "Registering char device failed with %d\n", Major);
                return Major;
        }

        printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
        //printk(KERN_INFO "Assigned major number: %d. Now create the dev file: \"/dev/%s\"\n", Major,DEVICE_NAME);

        gdata = kzalloc(sizeof(struct data), GFP_KERNEL);
        BUG_ON(!gdata);


        atomic_set(&open_count, 0);

        if(sysfs_create_group(kernel_kobj, &lt_attr_group))
                    printk(KERN_INFO "can't create sysfs\n");

        BUG_ON(init_cs_handler(gdata));
        return 0;
}

  /*
   * This function is called when the module is unloaded
   */
void cleanup_module(void)
{
        free_cs_handler(gdata);
        kfree(gdata);
        sysfs_remove_group(kernel_kobj, &lt_attr_group);
        unregister_chrdev(Major, DEVICE_NAME);
}

MODULE_LICENSE("GPL");
