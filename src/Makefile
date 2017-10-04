all:
	gcc -pthread ksync_bench.c -o syncbench
	cd module; make
clean:
	rm -f syncbench; cd module; make clean
