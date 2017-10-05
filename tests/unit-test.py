#!/usr/bin/python

import subprocess
import os

def change_lock(lock_num):
    os.system("echo " + str(lock_num) + " > /sys/kernel/asg2_lock")

def execute(args):
    popen = subprocess.Popen(args, stdout=subprocess.PIPE)
    popen.wait()
    output = popen.stdout.read()
    print output

def test(lock):
    if(lock == "spinlock"):
        change_lock(1)
    elif(lock == "rwlock"):
        change_lock(2)
    elif(lock == "seqlock"):
        change_lock(3)
    elif(lock == "rcu"):
        change_lock(4)
    elif(lock=="rwlock_custom"):
        change_lock(5)
    
    args = ("../src/syncbench", "8", "50000", "99", "1")
    execute(args)

def start():
    print("Starting correctness tests for locking implementations..\n")
    print("Spinlock testing..\n")
    test("spinlock")
    print("RWlock testing..\n")
    test("rwlock")
    print("Seqlock testing..\n")
    test("seqlock")
    print("RCUlock testing..\n")
    test("rcu")
    print("RWlock_custom testing..\n")
    test("rwlock_custom")

if __name__ == '__main__':
    start()
