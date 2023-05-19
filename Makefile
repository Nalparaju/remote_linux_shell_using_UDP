CC=gcc
CFLAGS=-g -Wall
CLIBS=-pthread

all: server client

server: server.o
	$(CC) $(CFLAGS) -o server server.o $(CLIBS)

client: client.o
	$(CC) $(CFLAGS) -o client client.o

clean:
	rm -f *.o server client
