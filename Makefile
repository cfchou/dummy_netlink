all: *.c
	gcc -g -Wall -c rr_nlk.c
	gcc -g -Wall -o rr_nlk rr_nlk.o
	gcc -g -Wall -c ww_nlk.c
	gcc -g -Wall -o ww_nlk ww_nlk.o
obj-m += dummy_netlink.o
