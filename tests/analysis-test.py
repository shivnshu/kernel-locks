#!/usr/bin/python

import subprocess
import os

def change_lock(lock_num):
    os.system("echo " + str(lock_num) + " > /sys/kernel/asg2_lock")

def execute_and_get_time(args):
    outputs = []
    for i in range(20):
        popen = subprocess.Popen(args, stdout=subprocess.PIPE)
        popen.wait()
        output = popen.stdout.read()
        output = output.split("\n")[0].split()
        outputs.append(long(output[-1]))
    mean = sum(outputs)/len(outputs)
    outputs[:] = [abs(x-mean) for x in outputs]
    deviation = sum(outputs)/len(outputs)
    return (mean, deviation)

def out(num_threads, total_ops, per_reads, per_writes):
    args = ("../src/syncbench", num_threads, total_ops, per_reads, per_writes)
    (mean, deviation) = execute_and_get_time(args)
    print("Num_threads=%s, ops/thread=%s, per_read=%s, per_write=%s, Avg time: %d, Avg deviation: %d" % (num_threads, total_ops, per_reads, per_writes, mean, deviation))

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
