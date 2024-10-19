CC	 	= gcc
LD	 	= gcc
CFLAGS	 	= -Wall -g

LDFLAGS	 	= 
DEFS 	 	=

all:	sendfile recvfile

server: sendfile.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o sendfile sendfile.cpp

client: recvfile.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o recvfile recvfile.cpp

clean:
	rm -f *.o
	rm -f *~
	rm -f core.*
	rm -f sendfile
	rm -f recvfile