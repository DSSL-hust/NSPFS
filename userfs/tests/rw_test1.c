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


struct fs_stat {
	short type;		// Type of file
	int dev;		// File system's disk device id
	uint32_t ino;	// Inode number
	short nlink;	// Number of links to file
	uint32_t size;	// Size of file in bytes

	struct timeval mtime;
	struct timeval ctime;
	struct timeval atime;
};

int main(){
	
	char test_dir_prefix[256] = "/mnt/pmem_emul";
	char test_dir[256]= "/mnt/pmem_emul/fuckdir";
	char test_file[256]="/mnt/pmem_emul/test.txt";
	unsigned long file_size_bytes;
	char *buf;
	char *buf1;
	char *buf2;
	char *buf3;
	//buf = new char[(2 << 21)];
	init_fs();
	userfs_posix_mkdir(test_dir,0777);
	//int count =100;
	//for(int i=0;i< count;i++){
	//printf("i %d\n",i);
	int fd = userfs_posix_open(test_file, O_RDWR| O_CREAT| O_DIRECT, 0666);
	file_size_bytes= 524288;
	buf = new char[file_size_bytes];
	for (int j = 0; j < file_size_bytes; j++) {
		buf[j] = '0' + (j % 10);
	}
	printf("buf %s\n",buf+file_size_bytes-10);
	int ret = userfs_posix_write(fd, buf, file_size_bytes);
	
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	for (int j = 0; j < file_size_bytes; j++) {
		buf[j] = 'a' + (j % 10);
	}
	
	printf("buf %s\n",buf+file_size_bytes-10);
	
	ret = userfs_posix_write(fd, buf, file_size_bytes);
	
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	
	for (int j = 0; j < file_size_bytes; j++) {
		buf[j] = 'A' + (j % 10);
	}
	printf("buf %s\n",buf+file_size_bytes-10);
	ret = userfs_posix_write(fd, buf, file_size_bytes);
	
	if (ret != file_size_bytes) {
		//printf("write request %u received len %d\n", io_size, ret);
		errx(1, "write");
	}
	
	
	memset(buf,0,file_size_bytes);
	
	userfs_posix_lseek(fd, 0, SEEK_SET);
	printf("read1 %d\n",fd);
	ret = userfs_posix_read(fd, buf, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		printf("read buf %s\n",buf + file_size_bytes-10 );
	}
	
	userfs_posix_close(fd);
	
	shutdown_fs();
/*	
	
	init_fs();
	fd = userfs_posix_open(test_file, O_RDWR| O_DIRECT, 0666);
	//memset(buf,0,file_size_bytes);
	printf("read2\n");
	file_size_bytes = 1048576;
	buf1 = new char[file_size_bytes];
	ret = userfs_posix_read(fd, buf1, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		printf("read buf %s\n",buf1+1048500);
	}
	
	printf("read3\n");
	//memset(buf,0,file_size_bytes);
	file_size_bytes=1048576*2;
	buf2 = new char[file_size_bytes];
	//userfs_posix_lseek(fd, 0, SEEK_SET);
	ret = userfs_posix_read(fd, buf2, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		//printf("read buf %s\n",buf2);
	}
/*	
	printf("read4\n");
	//memset(buf,0,file_size_bytes);
	file_size_bytes=1048576;
	buf3 = new char[file_size_bytes];
	//userfs_posix_lseek(fd, 0, SEEK_SET);
	ret = userfs_posix_read(fd, buf3, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		printf("read buf %s\n",buf3+1048500);
	}


	
	struct stat stat;
	//stat=(struct stat *)malloc(sizeof(struct stat));
	userfs_posix_stat(test_file,&stat);
	printf("stat->ino %d, stat->size %d\n",stat.st_ino,stat.st_size);
	printf("stat->st_blocks %d, stat->st_blksize %d\n",stat.st_blocks,stat.st_blksize);
	
	userfs_posix_close(fd);
	userfs_posix_unlink(test_file);
	*/
}

