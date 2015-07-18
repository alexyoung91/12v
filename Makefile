CC = gcc
CFLAGS = -std=gnu99 -g -Wall -pthread
INCLUDES = -I/usr/local/include
LDFLAGS = -lbcm2835 -lcurl -lncurses -lpcre
OBJS = gusts.o main.o mcp3424.o
PROGRAM = main

all: main

main: $(OBJS)
	 $(CC) $(CFLAGS) $(INCLUDES) -o $(PROGRAM) $(OBJS) $(LDFLAGS)

gusts.o: gusts.h gusts.c
main.o: main.c
mcp3424.o: mcp3424.h mcp3424.c

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

clean:
	rm -f *.o main

.PHONY: clean
