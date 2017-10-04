#!/usr/bin/python

import subprocess
import os

def execute(args):
    popen = subprocess.Popen(args, stdout=subprocess.PIPE)
    popen.wait()
    output = popen.stdout.read()
    print output

def test(lock):
    if(lock == "spinlock"):
        os.system("echo 1 > /sys/kernel/asg2_lock")
    elif(lock == "rwlock"):
        os.system("echo 2 > /sys/kernel/asg2_lock")
    elif(lock == "seqlock"):
        os.system("echo 3 > /sys/kernel/asg2_lock")
    elif(lock=="rwlock_custom"):
        os.system("echo 4 > /sys/kernel/asg2_lock")
    
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
    print("RWlock_custom testing..\n")
    test("rwlock_custom")

if __name__ == '__main__':
    start()
