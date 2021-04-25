#!/bin/sh

./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt
./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt 
./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt 
./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt 
./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt 
./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt 
./clearcache.sh
./io_test sr 1000M 256K 1 >>/root/256_1.txt 

./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt
./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt
./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt
./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt
./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt
./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt
./clearcache.sh
./io_test sr 1000M 256K 2 >>/root/256_2.txt

./clearcache.sh
./io_test sr 1000M 256K 4 >>/root/256_4.txt
./clearcache.sh
./io_test sr 1000M 256K 4 >>/root/256_4.txt
./clearcache.sh
./io_test sr 1000M 256K 4 >>/root/256_4.txt
./clearcache.sh
./io_test sr 1000M 256K 4 >>/root/256_4.txt
./clearcache.sh
./io_test sr 1000M 256K 4 >>/root/256_4.txt
./clearcache.sh
./io_test sr 1000M 256K 4 >>/root/256_4.txt

./clearcache.sh
./io_test sr 1000M 256K 8 >>/root/256_8.txt
./clearcache.sh
./io_test sr 1000M 256K 8 >>/root/256_8.txt
./clearcache.sh
./io_test sr 1000M 256K 8 >>/root/256_8.txt
./clearcache.sh
./io_test sr 1000M 256K 8 >>/root/256_8.txt
./clearcache.sh
./io_test sr 1000M 256K 8 >>/root/256_8.txt
./clearcache.sh
./io_test sr 1000M 256K 8 >>/root/256_8.txt

./clearcache.sh
./io_test sr 500M 256K 16 >>/root/256_16.txt
./clearcache.sh
./io_test sr 500M 256K 16 >>/root/256_16.txt
./clearcache.sh
./io_test sr 500M 256K 16 >>/root/256_16.txt
./clearcache.sh
./io_test sr 500M 256K 16 >>/root/256_16.txt
./clearcache.sh
./io_test sr 500M 256K 16 >>/root/256_16.txt
./clearcache.sh
./io_test sr 500M 256K 16 >>/root/256_16.txt
