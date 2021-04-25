#ifndef _POSIX_INTERFACE_H_
#define _POSIX_INTERFACE_H_

#include <sys/stat.h>
#include "global/global.h"

#ifdef __cplusplus
extern "C" {
#endif

int userfs_posix_open(char *path, int flags, unsigned short mode);
int userfs_posix_access(char *pathname, int mode);
int userfs_posix_creat(char *path, uint16_t mode);
int userfs_posix_read(int fd, void *buf, int count);
int userfs_posix_pread64(int fd, void *buf, int count, loff_t off);
int userfs_posix_write(int fd, void *buf, int count);
int userfs_posix_lseek(int fd, int64_t offset, int origin);
int userfs_posix_mkdir(char *path, unsigned int mode);
int userfs_posix_rmdir(char *path);
int userfs_posix_close(int fd);
int userfs_posix_stat(const char *filename, struct stat *stat_buf);
int userfs_posix_fstat(int fd, struct stat *stat_buf);
int userfs_posix_fallocate(int fd, offset_t offset, offset_t len);
int userfs_posix_unlink(const char *filename);
int userfs_posix_truncate(const char *filename, offset_t length);
int userfs_posix_ftruncate(int fd, offset_t length);
int userfs_posix_rename(char *oldname, char *newname);
size_t userfs_posix_getdents(int fd, struct linux_dirent *buf, size_t count);
int userfs_posix_fcntl(int fd, int cmd, void *arg);

#ifdef __cplusplus
}
#endif

#endif
