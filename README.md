# Project 2: Reliable File Transfer Protocol
============================================
## Design and implement
   ### language C++
   ### Main file: 
      +sender.cpp
      +recevier.cpp
      +Make
   ### Main getopt(), UDP, Sliding Window
   Using sliding window to transfer the whole file.
   When we are trying to send a file:
   1. Firstly, we will send the directory
   2. Secondly, the filedirectory
   3. Thirdly, the filename
   4. Fourthly, loop all the data
   5. Finally, Send EOF packet to check if recieved the full file.
   suppose Window_Size = x
   [0],[1],[2],[3], ... ,[x-1],[x]
   head                        tail
   
   

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
     $ /usr/bin/netsim --delay 20 --drop 20
     ```
5. Make in both look and clear
```
$ make
```
6. Clear run command reciever first
```
$ ./receiver -p 18020
```
8. Look run command sender then:
```
$ ./sender -r 128.42.124.178:18020 -f ./file.bin
```
or
```
$ ./sender -r opal.clear.rice.edu:18020 -f ./file.bin
```
9. Check if the file was transfered reliable
```
$ md5sum file.bin file.bin.recv
```
