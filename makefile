CC = gcc
CFLAGS = -Wall -Werror -pedantic --std=c99 -O2

hkd: hkd.o

debug:
	gcc $(CFLAGS) -g hkd.c -o hkd_debug

hkd.o: hkd.c

clean:
	rm *.o hkd hkd_debug 2> /dev/null
