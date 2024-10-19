files:
Makefile
recvfile.cpp
sendfile.cpp
testfile.bin
testfile_10MB.bin
testfile_25MB.bin


commands

make
make clean


./recvfile -p 18000
./sendfile -r 127.0.0.1:18000 -f ./testfile.bin
./sendfile -r 127.0.0.1:18000 -f ./testfile_10MB.bin
./sendfile -r 127.0.0.1:18000 -f ./testfile_25MB.bin

check contents:
md5sum testfile.bin testfile.bin.recv
md5sum testfile_10MB.bin testfile_10MB.bin.recv
