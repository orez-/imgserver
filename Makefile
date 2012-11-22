# Second one for gdb debugging
CC = gcc
# CC = gcc -g -O0

all: clean server client

server:	server.h server.o memory.h memory.o
			$(CC) server.o memory.o -o server -lpthread -lm

client: client.h client.o memory.h memory.o
			$(CC) client.o memory.o -o client -lreadline

clean:
	rm -f *.o server client
