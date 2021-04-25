./uio_setup.sh
./run.sh io_test sw 100M 4K 32
sleep 45
./run.sh io_test sw 100M 8K 32
sleep 20
./run.sh io_test sw 100M 16K 32
sleep 5
./run.sh io_test sw 100M 32K 32
sleep 5
./run.sh io_test sw 100M 64K 32
sleep 5
./run.sh io_test sw 100M 128K 32

