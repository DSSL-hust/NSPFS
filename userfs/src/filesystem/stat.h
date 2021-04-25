#ifndef _STAT_H_
#define _STAT_H_

#include "global/global.h"

struct fs_stat {
	short type;		// Type of file
	int dev;		// File system's disk device id
	uint32_t ino;	// Inode number
	short nlink;	// Number of links to file
	uint32_t size;	// Size of file in bytes

	userfs_time_t mtime;
	userfs_time_t ctime;
	userfs_time_t atime;
};

static inline void userfs_get_time(userfs_time_t *t)
{
	gettimeofday(t, NULL);
	return;
}

#endif
