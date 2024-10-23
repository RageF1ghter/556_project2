128.42.124.178
/usr/bin/netsim --delay 20 --drop 20 --mangle
/usr/bin/netsim --delay 0 --drop 0
./sender 128.42.124.178 18020 ./file.txt
