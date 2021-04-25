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
	char test_dir[256]= "/mnt/pmem_emul/fuckdir";
	char test_file[256]="/mnt/pmem_emul/fuckdir/fuckfile";
	unsigned long file_size_bytes;
	char *buf1;
	init_fs();
	int fd = userfs_posix_open(test_file, O_RDWR| O_DIRECT, 0666);
	//memset(buf,0,file_size_bytes);
	printf("read2\n");
	buf1 = new char[65536];
	int ret = userfs_posix_read(fd, buf1, file_size_bytes);
	if (ret != file_size_bytes) {
		printf("read size mismatch: return %d, request %lu\n",ret, file_size_bytes);
	}else{
		printf("read size %d\n",ret);
		//printf("read buf %s\n",buf1);
	}
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

