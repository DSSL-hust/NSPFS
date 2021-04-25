#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "userfs/userfs_interface.h"

#define TESTDIR "/mnt/pmem_emul/test_dir"
//#define N_FILES 1000
#define N_FILES 10

int main(int argc, char ** argv)
{
    struct stat buf;
    int i, ret = 0;
	
	init_fs();

	/*
    if ((ret = lstat(TESTDIR "/files/files1", &buf)) == 0) {
		printf("%s: inode number %lu", TESTDIR "files/file1", buf.st_ino);
	}
	*/

	/*
    if ((ret = rmdir(TESTDIR)) < 0 && errno != ENOENT) {
        perror("rmdir");
        exit(1);
    }
	*/
    printf("-------userfs_posix_mkdir %s\n",TESTDIR);
    if ((ret = userfs_posix_mkdir(TESTDIR, 0700)) < 0) {
        perror("mkdir");
        exit(1);
    }


    printf("-------userfs_posix_creat %s/file\n",TESTDIR);
    if ((ret = userfs_posix_creat(TESTDIR "/file", 0600)) < 0) {
        perror("open");
        exit(1);
    }

    

    printf("-------userfs_posix_unlink %s/file\n",TESTDIR);
    if ((ret = userfs_posix_unlink(TESTDIR "/file")) < 0) {
        perror("unlink");
        exit(1);
    }
    synclog();
    printf("-------userfs_posix_mkdir %s/files\n",TESTDIR);
    if ((ret = userfs_posix_mkdir(TESTDIR "/files", 0600)) < 0) {
        perror("open");
        exit(1);
    }


	for (i = 0; i < N_FILES; i++) {
		char file_path[4096];
		memset(file_path, 0, 4096);

        printf("-------userfs_posix_creat %s/files/file%d\n",TESTDIR,i);
		sprintf(file_path, "%s%d", TESTDIR "/files/file", i);
		if ((ret = userfs_posix_creat(file_path, 0600)) < 0) {
			perror("open");
			exit(1);
		}
	}
    printf("-------userfs_posix_unlink %s/files/file2\n",TESTDIR);
    if ((ret = userfs_posix_unlink(TESTDIR "/files/file2")) < 0) {
        perror("unlink");
        exit(1);
    }

    printf("-------userfs_posix_creat %s/files/file2\n",TESTDIR);
	if ((ret = userfs_posix_creat(TESTDIR "/files/file2", 0600)) < 0) {
		perror("open");
		exit(1);
	}
    //synclog();
	//shutdown_fs();

    return 0;
}
