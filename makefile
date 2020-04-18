CC = gcc
CFLAGS = -Wall -Werror -pedantic -O2

macrod: macrod.o

macrod.o: macrod.c

clean:
	rm macrod *.o
