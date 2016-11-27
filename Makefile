CC = gcc
CFLAGS = -g
all:: test

test: test.o
	$(CC) -w -o test my_queue.o memlib.o
	make clean
test.o: myqueue
	$(CC) -g -w -c memlib.c -o memlib.o
myqueue:
	$(CC) -g -w -c my_queue.c -o my_queue.o
clean:
	rm *.o
