CC		= g++
LD		= g++
CFLAGS	= -Wall -g

LDFLAGS	= 
DEFS 	=

all:	client server

server: server.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o server server.cpp

client:	client.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o client client.cpp

clean:
	rm -f *.o
	rm -f *~
	rm -f core.*
	rm -f server
	rm -f client
