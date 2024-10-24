# Project 2 Reliable File Transfer Protocol
===========================================
## Design and implement


## Test commands
Set up the environment


1. Log in to the look.cs.rice.edu
```
$ ssh <username>comp429@look.cs.rice.edu
```
2. Log in to the clear
```
$ ssh <username>@opal.clear.rice.edu
```
3. Set net environment
   i. Reset environment
    ```
    $ /usr/bin/netsim
    ```
   ii. Test structure :
   /usr/bin/netsim [--delay <percent>] [--drop <percent>]
                    [--reorder <percent>] [--mangle <percent>]
                    [--duplicate <percent>]
     ```
     $ /usr/bin/netsim --delay 20 --drop 20
     ```
5. 

1. Make
```
$ make
```
2. 



files:
<br>Makefile
<br>recvfile.cpp
<br>sendfile.cpp
<br>testfile.bin
<br>testfile_10MB.bin
<br>testfile_25MB.bin


commands

make
make clean


./recvfile -p 18000
<br>./sendfile -r 127.0.0.1:18000 -f ./testfile.bin
<br>./sendfile -r 127.0.0.1:18000 -f ./testfile_10MB.bin
<br>./sendfile -r 127.0.0.1:18000 -f ./testfile_25MB.bin

<br>check contents:
<br>md5sum testfile.bin testfile.bin.recv
<br>md5sum testfile_10MB.bin testfile_10MB.bin.recv

<br> ./sendfile -r opal.clear.rice.edu:18000 -f ./testfile.bin
