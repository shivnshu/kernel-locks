#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define MAX_THREADS 32
#define SEED_BASE 0x65453
int fd = -1;
int barrier = 1;

struct action{
                pthread_t thread;
                unsigned long total_ops;
                int read_perc;
                int write_perc;
                unsigned seed;
                /*stats*/
                unsigned long num_reads;
                unsigned long total_read_cycles;
                unsigned long num_writes;
                unsigned long total_write_cycles;
                
};

#if __x86_64__ 
static unsigned long tick(void)
{
	unsigned long var;
	unsigned hi, lo;

	__asm volatile
	    ("rdtsc" : "=a" (lo), "=d" (hi));

	var = ((unsigned long)hi << 32) | lo;
	return (var);
}
#endif

void *parops(void *arg)
{
   long i;
   char buf[64];
   struct action *ac = (struct action *) arg; 
  
   while(barrier);

   for(i=0; i < ac->total_ops; ++i){
       int retval = rand_r(&ac->seed) % 100;
       if(retval < ac->read_perc){
           unsigned long start, end;
           long num_writes, counter, result;
           start = tick();
           retval = read(fd, buf, 64);
           if(retval < 0){
               perror("IO");
               pthread_exit(NULL);
           }       
           sscanf(buf, "%ld %ld %ld", &num_writes, &counter, &result);
           if(num_writes != counter || (num_writes + counter != result)){
               printf("num_writes = %ld counter = %ld result = %ld\n", num_writes, counter, result); 
               assert(num_writes == counter && (num_writes+counter == result));
           }
           end = tick();
           ac->num_reads++;
           ac->total_read_cycles += end - start;
        }
       else{
           unsigned long start, end;
           start = tick();
           retval = write(fd, buf, 64);
           end = tick();
           ac->num_writes++;
           ac->total_write_cycles += end - start;
       }
  }
  return NULL;
}

int main (int argc, char **argv)
{
   struct action *actions; 
   int i;
   unsigned long start, end, total_ops;
   int read_perc, write_perc, total_threads;
   
    if (argc != 5){
          printf("Usage: %s <numthreads> <ops/thread> <readops (%%)> <writeops (%%)>\n", argv[0]);
          exit(-1);
    }

   total_threads = atoi(argv[1]);
   total_ops = atol(argv[2]);
   read_perc  = atoi(argv[3]);
   write_perc = atoi(argv[4]);
   
   if(total_threads < 0 || total_threads > MAX_THREADS || read_perc < 0 || write_perc < 0 || read_perc + write_perc != 100){
          printf("Usage: %s <numthreads> <ops/thread> <readops (%%)> <writeops (%%)>\n", argv[0]);
          exit(-1);
   }

   fd = open ("/dev/syncdev", O_RDWR);
   if(fd < 0){
         perror("open");
         exit(-1);
   }     
  
  actions = malloc(total_threads * sizeof(struct action));
  bzero(actions, total_threads * sizeof(struct action));
 
  for(i=0; i < total_threads; ++i){
        struct action *ac = actions + i;
        ac->total_ops = total_ops;
        ac->read_perc = read_perc;
        ac->write_perc = write_perc;
        ac->seed = SEED_BASE + i;
        assert(pthread_create(&ac->thread, NULL, parops, ac) == 0);
        
  }
   start = tick();
   barrier = 0;
   
   for(i=0; i < total_threads; ++i){
        void *ptr;
        struct action *ac = actions + i;
        pthread_join(ac->thread, &ptr); 
   }
   end = tick();
   printf("Total time taken = %lu\n", end - start);
   printf("----------- Thread Stats ------------\n");
   for(i=0; i < total_threads; ++i){
        struct action *ac = actions + i;
        printf("Thread = %d num_reads = %lu num_writes= %lu read_cycles= %lu write_cycles = %lu\n", i+1, ac->num_reads, ac->num_writes, ac->total_read_cycles, ac->total_write_cycles); 
   }
   
  
}
