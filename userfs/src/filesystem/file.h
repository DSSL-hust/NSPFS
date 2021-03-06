#ifndef _FILE_H_
#define _FILE_H_

#include "filesystem/stat.h"
#include "global/global.h"

typedef enum { FD_NONE, FD_PIPE, FD_INODE, FD_DIR } fd_type_t;
struct file {
	fd_type_t type;
	int fd;
	int ref; // reference count
	uint8_t readable;
	uint8_t writable;
	struct inode *ip;
	offset_t off;
	char filename[256];
	pthread_rwlock_t rwlock;
};

// opened file table, indexed by file descriptor
struct open_file_table {
	pthread_spinlock_t lock;
	//pthread_rwlock_t ftlock;
	struct file open_files[g_max_open_files];
};

extern struct open_file_table g_fd_table;
//extern struct fuckfs_ssdaddr *SSDADDR;

#define CONSOLE 1

// APIs
void userfs_file_init(void);
struct file *userfs_file_alloc(void);
struct file *userfs_file_dup(struct file *f);

int userfs_file_close(struct file *f);
//struct inode *userfs_object_create(char *path, unsigned short mode);
struct inode *userfs_open_file(char *path, unsigned short mode);

void reclaim_block(struct inode* inode);

int userfs_file_stat(struct file *f, struct stat *st);
int userfs_file_read(struct file *f, uint8_t *buf, int n);
int userfs_file_read_offset(struct file *f, uint8_t *bug, 
		size_t n, offset_t off);
int userfs_file_write(struct file *f, uint8_t *buf, size_t n);

#endif
