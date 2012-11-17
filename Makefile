CFLAGS = -Wall -ansi -pedantic
PORT=5656

all:
	gcc -ggdb server.c -o server.o -lpthread
	gcc -ggdb client.c -o client.o -lpthread

clean:
	rm -f *.o

server:
	./server.o $(PORT)

client:
	./client.o `hostname` $(PORT)
