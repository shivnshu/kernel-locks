#!/usr/bin/python

import subprocess
import os

def change_lock(lock_num):
    os.system("echo " + str(lock_num) + " > /sys/kernel/asg2_lock")

def execute_and_get_time(args):
    total_times = []
    read_times = []
    write_times = []
    for i in range(10):
        popen = subprocess.Popen(args, stdout=subprocess.PIPE)
        popen.wait()
        output = popen.stdout.read()
        output = output.split("\n")
        total_times.append(long((output[0].split())[-1]))
        for tstr in output[2:]:
            if (len(tstr)<13):
                break;
            out = tstr.split()
            read_times.append(long(out[9])/long(out[5]))
            if (long(out[-1]) != 0):
                write_times.append(long(out[-1])/long(out[-6]))
            else:
                write_times.append(0)

    avg_total_time = sum(total_times)/len(total_times)
    total_times[:] = [abs(x-avg_total_time) for x in total_times]
    deviation = sum(total_times)/len(total_times)
    avg_read_time = sum(read_times)/len(read_times)
    avg_write_time = sum(write_times)/len(write_times)
    return (avg_total_time, deviation, avg_read_time, avg_write_time)

def out(num_threads, total_ops, per_reads, per_writes):
    args = ("../src/syncbench", num_threads, total_ops, per_reads, per_writes)
    (mean, deviation, avg_read_time, avg_write_time) = execute_and_get_time(args)
    print("Num_threads=%s, ops/thread=%s, per_read=%s, per_write=%s, Avg time: %d, Avg deviation: %d, Avg readtime: %d, Avg writetime: %d" % (num_threads, total_ops, per_reads, per_writes, mean, deviation, avg_read_time, avg_write_time))

def analyse(lock):
    print("For " + lock + ":")
    out("8", "50000", "100", "0")
    out("8", "50000", "99", "1")
    out("8", "50000", "95", "5")
    print("\n")

if __name__ == "__main__":
    change_lock(1)
    analyse("spinlock")
    change_lock(2)
    analyse("RWlock")
    change_lock(3)
    analyse("Seqlock")
    change_lock(4)
    analyse("RCUlock")
    change_lock(5)
    analyse("RW custom lock")
