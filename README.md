128.42.124.178
/usr/bin/netsim --delay 20 --drop 20 --mangle 10
/usr/bin/netsim --delay 0 --drop 0
./sender 128.42.124.178 18020 ./file.txt


dd if=/dev/zero bs=1M count=1000 | nc 128.42.124.178 18020
nc -l 18020 > /dev/null