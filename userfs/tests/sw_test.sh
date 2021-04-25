#./uio_setup.sh
./io_test sw 500M 4K 1
sleep 10
./io_test sw 500M 4K 2
sleep 10
./io_test sw 500M 4K 4
sleep 10
./io_test sw 500M 4K 8
sleep 10
./io_test sw 500M 4K 16
sleep 10
./io_test sw 500M 4K 32

./io_test sw 500M 8K 1
sleep 10
./io_test sw 500M 8K 2
sleep 10
./io_test sw 500M 8K 4
sleep 10
./io_test sw 500M 8K 8
sleep 10
./io_test sw 500M 8K 16
sleep 5
./io_test sw 500M 8K 32

./io_test sw 500M 16K 1
sleep 10
./io_test sw 500M 16K 2
sleep 10
./io_test sw 500M 16K 4
sleep 10
./io_test sw 500M 16K 8
sleep 10
./io_test sw 500M 16K 16
sleep 5
./io_test sw 500M 16K 32

./io_test sw 500M 32K 1
sleep 10
./io_test sw 500M 32K 2
sleep 10
./io_test sw 500M 32K 4
sleep 10
./io_test sw 500M 32K 8
sleep 10
./io_test sw 500M 32K 16
sleep 5
./io_test sw 500M 32K 32

./io_test sw 500M 64K 1
sleep 10
./io_test sw 500M 64K 2
sleep 10
./io_test sw 500M 64K 4
sleep 10
./io_test sw 500M 64K 8
sleep 10
./io_test sw 500M 64K 16
sleep 5
./io_test sw 500M 64K 32

