# Project 2: Reliable File Transfer Protocol
======================================================
## Design and implement
   ### language C++
   ### Main file: 
      + sendfile.cpp
      + recvfile.cpp
      + Make
   ### Main 
   - getopt(), UDP, Sliding Window
   - gettimeofday() to get the transfer time
   - Using sliding window to transfer the whole file.
   - When we are trying to send a file:
   1. Firstly, we will send the directory
   2. Secondly, the filename
   3. Thirdly, loop all the data
   4. Finally, Send EOF packet to check if recieved the full file.
   - suppose Window_Size = x
   - [0],[1],[2],[3], ... ,[x-1],[x]
   - head                        tail
   - filedirectory will occupy window[0]
   - filename will ocucpy window[1]
   - start send data:
      - keep sending data packet: within window size - 2 
      - check receive data(according to receive logic):
        - when the acknum = 0 ---> The packet hasn't Acked yet
        - when the acknum = 1 ---> already received this packet
        - when the acknum = 2 ---> Packet corrupted need retrans
        
      - Only when received the most head data packet, slide the window, head++
      - send new packets inside window tail++
      - Acknum = 2 or timeout will trigger the retransmission
      - Since the ack can be lost, the sender will not be able to know the status. We maintain a hashmap to track each packet's ack status.
   
   

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
   - Reset environment
    ```
    $ /usr/bin/netsim
    ```
   - Test structure :
   /usr/bin/netsim [--delay <percent>] [--drop <percent>]
                    [--reorder <percent>] [--mangle <percent>]
                    [--duplicate <percent>]
    ```
    $ /usr/bin/netsim --delay 10 --drop 10 --reorder 10 --mangle 10 --duplicate 10
    ```
5. Make in both look and clear
```
$ make
```
6. Clear run command reciever first
```
$ ./recvfile -p 18020
```
8. Look run command sender then:
```
$ ./sendfile -r 128.42.124.178:18020 -f ./file.bin
```
9. Check if the file was transfered reliable
```
$ md5sum file.bin file.bin.recv
```
