CFLAGS = -Wall -ansi -pedantic
PORT=5656

all:
	gcc server.c -o server.o
	gcc client.c -o client.o

clean:
	rm -f *.o

server:
	./server.o $(PORT)

client:
	./client.o `hostname` $(PORT)
