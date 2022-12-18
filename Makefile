CC = gcc
CFLAGS = -O -g

all: icsh

icsh: icsh.c
	$(CC) $(CFLAGS) -o icsh icsh.c

clean:
	rm -f *.o icsh
