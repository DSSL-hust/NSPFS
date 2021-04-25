#!/bin/sh

./io_test sw 1000M 4K 1 >>/root/1.txt
sleep 1
./io_test sw 1000M 4K 1 >>/root/1.txt
sleep 1
./io_test sw 1000M 4K 1 >>/root/1.txt
sleep 1
./io_test sw 1000M 4K 1 >>/root/1.txt
sleep 1
./io_test sw 1000M 4K 1 >>/root/1.txt
sleep 1

./io_test sw 1000M 4K 2 >>/root/2.txt
sleep 1
./io_test sw 1000M 4K 2 >>/root/2.txt
sleep 1
./io_test sw 1000M 4K 2 >>/root/2.txt
sleep 1
./io_test sw 1000M 4K 2 >>/root/2.txt
sleep 1
./io_test sw 1000M 4K 2 >>/root/2.txt
sleep 1

./io_test sw 1000M 4K 4 >>/root/4.txt
sleep 1
./io_test sw 1000M 4K 4 >>/root/4.txt
sleep 1
./io_test sw 1000M 4K 4 >>/root/4.txt
sleep 1
./io_test sw 1000M 4K 4 >>/root/4.txt
sleep 1
./io_test sw 1000M 4K 4 >>/root/4.txt
sleep 1

./io_test sw 1000M 4K 8 >>/root/8.txt
sleep 1
./io_test sw 1000M 4K 8 >>/root/8.txt
sleep 1
./io_test sw 1000M 4K 8 >>/root/8.txt
sleep 1
./io_test sw 1000M 4K 8 >>/root/8.txt
sleep 1
./io_test sw 1000M 4K 8 >>/root/8.txt
sleep 1

./io_test sw 1000M 4K 16 >>/root/16.txt
sleep 1
./io_test sw 1000M 4K 16 >>/root/16.txt
sleep 1
./io_test sw 1000M 4K 16 >>/root/16.txt
sleep 1
./io_test sw 1000M 4K 16 >>/root/16.txt
sleep 1
./io_test sw 1000M 4K 16 >>/root/16.txt
sleep 1

./io_test sw 500M 4K 32 >>/root/32.txt
sleep 1
./io_test sw 500M 4K 32 >>/root/32.txt
sleep 1
./io_test sw 500M 4K 32 >>/root/32.txt
sleep 1
