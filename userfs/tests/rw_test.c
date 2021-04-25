#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <assert.h>

#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <random>
#include <memory>
#include <fstream>

#ifdef USERFS
#include <userfs/userfs_interface.h>	
#endif

#include "time_stat.h"
#include "thread.h"
#include "posix/posix_interface.h"



int main(){
	
	char test_dir_prefix[256] = "/mnt/pmem_emul";
	char test_dir[256]= "/mnt/pmem_emul";
	char test_file[256]="/mnt/pmem_emul/fuckfile";
	unsigned long file_size_bytes;
	char *buf;
	char *buf1;
	char *buf2;
	buf = new char[1048];
	init_fs();
	//userfs_posix_mkdir(test_dir,0777);
	int fd = userfs_posix_open(test_file, O_RDWR| O_CREAT| O_DIRECT, 0666);
	file_size_bytes= 1048;
	for (int j = 0; j < file_size_bytes; j++) {
		buf[j] = '0' + (j % 10);
	}
	printf("1userfs_posix_write %d\n",file_size_bytes);
	int ret = userfs_posix_write(fd, buf, file_size_bytes);
	
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	buf1=new char[261096];
	file_size_bytes= 261096;
	for (int j = 0; j < file_size_bytes; j++) {
		buf1[j] = '1' + (j % 10);
	}
	printf("2userfs_posix_write %d\n",file_size_bytes);
	ret = userfs_posix_write(fd, buf1, file_size_bytes);
	printf("2ret %d\n", ret);
	if (ret != file_size_bytes) {
		printf("write request %u received len %d\n", file_size_bytes, ret);
		errx(1, "write");
	}
	buf2=new char[1835008];
	file_size_bytes= 1835008;
	for (int j = 0; j < file_size_bytes; j++) {
		buf2[j] = '2' + (j % 10);
	}
	printf("3userfs_posix_write %d\n",file_size_bytes);
	ret = userfs_posix_write(fd, buf2, file_size_bytes);
	printf("3ret %d\n", ret);
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	char *buf3;
	buf3=new char[2090000];
	file_size_bytes= 2090000;
	for (int j = 0; j < file_size_bytes; j++) {
		buf3[j] = '3' + (j % 10);
	}
	printf("4userfs_posix_write %d\n",file_size_bytes);
	ret = userfs_posix_write(fd, buf3, file_size_bytes);
	printf("4ret %d\n", ret);
	if (ret != file_size_bytes) {
		printf("write request %u received len %d\n", file_size_bytes, ret);
		errx(1, "write");
	}
	char *buf4;
	buf4 = new char[9900];
	file_size_bytes= 9900;
	for (int j = 0; j < file_size_bytes; j++) {
		buf4[j] = '4' + (j % 10);
	}
	printf("5userfs_posix_write %d\n",file_size_bytes);
	ret = userfs_posix_write(fd, buf4, file_size_bytes);
	printf("5ret %d\n", ret);
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	userfs_posix_lseek(fd, 4190208, SEEK_SET);
	char *buf5;
	buf5 = new char[10000];
	file_size_bytes= 10000;
	for (int j = 0; j < file_size_bytes; j++) {
		buf5[j] = '5' + (j % 10);
	}
	printf("6userfs_posix_write %d\n",file_size_bytes);
	ret = userfs_posix_write(fd, buf5, file_size_bytes);
	printf("6ret %d\n", ret);
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
/*	*/
	userfs_posix_close(fd);
	shutdown_fs();
/*	
	printf("userfs_posix_write\n");
	ret = userfs_posix_write(fd, buf2, 20000);
	
	if (ret != 20000) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	printf("userfs_posix_write\n");
	ret = userfs_posix_write(fd, buf2, 65536);
	
	if (ret != 65536) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	*/
/*	userfs_posix_lseek(fd, 0, SEEK_SET);
	printf("read1\n");
	ret = userfs_posix_read(fd, buf, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		//printf("read buf %s\n",buf);
	}
	
	userfs_posix_close(fd);
	
	shutdown_fs();
	init_fs();
	fd = userfs_posix_open(test_file, O_RDWR| O_DIRECT, 0666);
	//memset(buf,0,file_size_bytes);
	printf("read2\n");
	buf1 = new char[65536];
	ret = userfs_posix_read(fd, buf1, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		//printf("read buf %s\n",buf1);
	}
	*/
	/*
	printf("read3\n");
	//memset(buf,0,file_size_bytes);
	
	file_size_bytes=211768;
	buf2 = new char[file_size_bytes];
	userfs_posix_lseek(fd, 0, SEEK_SET);
	ret = userfs_posix_read(fd, buf2, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		printf("read buf %s\n",buf2);
	}
	*/
	
}

