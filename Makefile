CC = gcc
CFLAGS = -O3 -Wall -Wextra -s 
LDFLAGS = -lsgutils2
BIN = leetcmd

all: leetcmd.c
	$(CC) $(CFLAGS) -o $(BIN) leetcmd.c $(LDFLAGS)
test: 
	$(CC) $(CFLAGS) -o test test.c $(LDFLAGS)
install:
	install $(BIN) -D $(DESTDIR)/usr/bin/$(BIN)
