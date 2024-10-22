CC		= g++
LD		= g++
CFLAGS	= -Wall -g

LDFLAGS	= 
DEFS 	=

all:	receiver sender

sender: sender.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o sender sender.cpp

receiver:	receiver.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o receiver receiver.cpp

clean:
	rm -f *.o
	rm -f *~
	rm -f core.*
	rm -f sender
	rm -f receiver
