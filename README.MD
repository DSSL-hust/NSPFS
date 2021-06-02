# NSPFS: An NVM-SSD Parallelized File System

NSPFS a novel hybrid NVM-SSD parallelized file system to fully exploit the desirable features of both NVM and SSD in a highly cost-effective manner.

NSPFS is developed and tested on Ubuntu 16.06.10, Linux kernel 4.4.0 and gcc version 5.4.0. 

This repository contains contains initial source code and tests. UserFS is linked with applications as a library.

## Build and Install NSPFS
Assume current directory is a project root directory.

1. **install PMDK**
UserFS user PMDK library to write data to NVM.
```shell
    git clone https://github.com/pmem/pmdk
    cd pmdk
    make
```

2. **build UserFS**
```shell
    cd userfs
    make
```

3. **install Kernelfs**
```shell
    insmod pmfs.ko
    mount -t pmfs -o init /dev/pmem0 /mnt/pmem_emul
    dd if=/dev/zero of=/mnt/pmem_emul/.log bs=1M count=8
```

## Running NSPFS
Assume current directory is a project root directory.

1. **running testing program**
```
    cd userfs/tests
    make
    sudo ./io_test sw 2G 4K 1
```
`sw`:test with sequential write
`2G`:test with 2G file
`4K`:test with 4K IO
`1`:test with 1 thread

2. **writing and compiling your program (for example, ./userfs/fio)**
* Include <userfs/userfs_interface.h> and #include "posix/posix_interface.h" header files in the source file with the file IO code.
* Upadte the POSIX interfaces (e.g., open, write, read, etc.) with our provided interfaces (e.g., userfs_posix_open, userfs_posix_write, userfs_posix_read, etc.).
* Update its Makefile with new linked library.

## NSPFS Configuration

In userfs/Makefile, search USERFS_FLAGS as keyword
```
USERFS_FLAGS = -DLIBFS
#USERFS_FLAGS = -DLIBFS -DUSERFS_PRINT -DUSERFS_INFO
#USERFS_FLAGS += -DCONCURRENT 
#USERFS_FLAGS += -DCONCURRENT -DEACHPOOL
USERFS_FLAGS += -DINVALIDATION
USERFS_FLAGS += -DKLIB_HASH
```
`DUSERFS_PRINT`  -print more infomation

`DCONCURRENT` - paralle IOs with threadpool













