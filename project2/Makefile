CC=g++

all:
	$(CC) -std=c++11 -g single_server.cpp -o ./single/s_server.o
	$(CC) -std=c++11 -g concurrent_server.cpp -o ./concurrent/c_server.o -lpthread -lrt
	$(CC) -g delayclient.c -o client.o
