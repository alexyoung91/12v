all: main

main: main.c mcp3424.h mcp3424.c
	gcc -std=gnu99 -g -Wall -I/usr/local/include -o main main.c mcp3424.c -lncurses -lbcm2835

clean:
	rm -f main

.PHONY: clean
