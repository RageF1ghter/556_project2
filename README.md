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
