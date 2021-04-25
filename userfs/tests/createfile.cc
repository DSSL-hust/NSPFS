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

#ifdef USERFS
const char *test_dir_prefix = "/mnt/pmem_emul/yang";
const char *test_dir_prefix1 = "/mnt/pmem_emul/yang1";
const char *test_dir_prefix2 = "/mnt/pmem_emul/yang2";
const char *test_dir_prefix3 = "/mnt/pmem_emul/yang3";
const char *test_dir_prefix4 = "/mnt/pmem_emul/yang4";
const char *test_dir_prefix5 = "/mnt/pmem_emul/yang5";
const char *test_dir_prefix6 = "/mnt/pmem_emul/yang6";
const char *test_dir_prefix7 = "/mnt/pmem_emul/yang7";
const char *test_dir_prefix8 = "/mnt/pmem_emul/yang8";
const char *test_dir_prefix9 = "/mnt/pmem_emul/yang9";
//const char *test_dir_prefix1 = "/mnt/pmem_emul/fuckfs";
//const char *test_dir_prefix2 = "/mnt/pmem_emul/yang1";
//const char *test_dir_prefix3 = "/mnt/pmem_emul/fuckfs1";

#else
const char *test_dir_prefix = "./pmem";
//const char test_dir_prefix[] = "./ssd";
#endif


int main(){
	init_fs();
	int fd;
	int ret;
	string test_file;
	int filenum= 500;
	char filename[256];
        int dev_id = 0;
	ret = mkdir(test_dir_prefix, 0777);

        if (ret < 0 && errno != EEXIST) {
                perror("mkdir\n");
                exit(-1);
        }

	for(int i=0;i<filenum;i++){
		test_file.assign(test_dir_prefix);
		test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
		//test_file += "/file1" + "-" + std::to_string(i);
		strcpy(filename,test_file.c_str());
		printf("filename %s\n",filename);
		if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
			perror("open\n");
            		exit(-1);
		}
		userfs_posix_close(fd);
	}
	ret = mkdir(test_dir_prefix1, 0777);

        if (ret < 0 && errno != EEXIST) {
                perror("mkdir\n");
                exit(-1);
        }

	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix1);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }
	
	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix2);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }

	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix3);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }
	
	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix4);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }

	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix5);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }

	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix6);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }
	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix7);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }

	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix8);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }
	
	for(int i=0;i<filenum;i++){
                test_file.assign(test_dir_prefix9);
                test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(i);
                //test_file += "/file1" + "-" + std::to_string(i);
                strcpy(filename,test_file.c_str());
                printf("filename %s\n",filename);
                if((fd = userfs_posix_open(filename, O_RDWR| O_CREAT, 0666)) < 0){
                        perror("open\n");
                        exit(-1);
                }
                userfs_posix_close(fd);
        }

	shutdown_fs();
	return 0;
}
