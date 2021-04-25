#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 500
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "userfs/userfs_user.h"
#include "global/global.h"
//#include <sys/ioctl.h> 

//#include <sys/mount.h> 
/*
#ifndef O_DIRECT
#define O_DIRECT 00040000
#endif
*/
static int ssdfd[g_n_devices + 1];
static struct stat f_stat[g_n_devices + 1];
#define BLOCKSIZE 4096

uint8_t *hdd_init(uint8_t dev, char *dev_path)
{
	int ret;
	//int fd;
	if (lstat(dev_path, &f_stat[dev]) == ENOENT) {
		ret = creat(dev_path, 0666);
		if (ret < 0) {
			perror("cannot create userfs hdd file\n");
			exit(-1);
		}
	}

	ssdfd[dev] = open(dev_path, O_RDWR|O_DIRECT, 0600);

	if (ssdfd[dev] < 0) {
		perror("cannot open the storage file\n");
		exit(-1);
	}

	ret = fstat(ssdfd[dev], &f_stat[dev]);
	if (ret < 0) {
		perror("cannot stat the storage file\n");
		exit(-1);
	}


	return 0;
}

int hdd_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	int ret;
	
	if(((unsigned long)buf&0x1ffUL)!=0){
		char *alignbuf;
		ret = posix_memalign(&alignbuf,512, io_size);
		if (ret) {
    		fprintf (stderr, "posix_memalign: %s\n", strerror (ret));
    		return -1;
		}
		ret = pread(ssdfd[dev], alignbuf, io_size, blockno << g_block_size_shift);
		memcpy(buf,alignbuf,io_size);
	}else{
		ret = pread(ssdfd[dev], buf, io_size, blockno << g_block_size_shift);
	}
	return ret;
}

int hdd_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	int ret;
	if(((unsigned long)buf&0x1ffUL)!=0){
		char *alignbuf;
		ret = posix_memalign(&alignbuf,512, io_size);
		if (ret) {
    		fprintf (stderr, "posix_memalign: %s\n", strerror (ret));
    		return -1;
		}
		memcpy(alignbuf,buf,io_size);
		ret = pwrite(ssdfd[dev], alignbuf, io_size, blockno<<g_block_size_shift);
	}else{
		ret = pwrite(ssdfd[dev], buf, io_size, blockno<<g_block_size_shift);
	}
	return ret;
}

int hdd_write_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, uint32_t io_size){
	int ret;
	char *appendbuf;
	ret = posix_memalign(&appendbuf,512, BLOCKSIZE);
	if (ret) {
    	fprintf (stderr, "posix_memalign: %s\n", strerror (ret));
    	return -1;
	}
	ret = pread(ssdfd[dev], appendbuf, offset, blockno << g_block_size_shift);
	memcpy(appendbuf+offset,buf,io_size);
	
	ret = pwrite(ssdfd[dev], appendbuf, BLOCKSIZE, blockno << g_block_size_shift);
	free(appendbuf);
	return io_size;
}

int hdd_erase(uint8_t dev, addr_t blockno, uint32_t io_size)
{
	return 0;
}

int hdd_commit(uint8_t dev)
{
	fsync(ssdfd[dev]);
	return 0;
}

void hdd_exit(uint8_t dev)
{
	close(ssdfd[dev]);
	return;
}
