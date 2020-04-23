CC = gcc
CFLAGS = -Wall -Werror -pedantic -O2

macrod: macrod.o

debug:
	gcc $(CFLAGS) -g macrod.c -o macrod_debug

macrod.o: macrod.c

clean:
	rm macrod *.o macrod_debug 2> /dev/null
