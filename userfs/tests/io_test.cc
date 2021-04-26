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
const char *test_dir_prefix = "/mnt/pmem_emul";
#else
const char *test_dir_prefix = "./pmem";
//const char test_dir_prefix[] = "./ssd";
#endif

#define ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define ALIGN_MASK_FLOOR(x, mask) (((x)) & ~(mask))
#define ALIGN(x, a)  ALIGN_MASK((x), ((__typeof__(x))(a) - 1))
#define ALIGN_FLOOR(x, a)  ALIGN_MASK_FLOOR((x), ((__typeof__(x))(a) - 1))
#define BUF_SIZE (2 << 20)

#define ODIRECT
//#undef ODIRECT
//#define VERIFY

typedef enum {SEQ_WRITE, SEQ_READ, SEQ_WRITE_READ, RAND_WRITE, RAND_READ, 
	ZIPF_WRITE, ZIPF_READ, ZIPF_MIX, NONE} test_t;

typedef enum {FS} test_mode_t;

typedef unsigned long addr_t;
uint8_t dev_id;
int flag =0;

pthread_barrier_t b;

class io_bench : public CThread 
{
	public:
		io_bench(int _id, unsigned long _file_size_bytes, unsigned int _io_size,
				test_t _test_type);

		int id, fd, per_thread_stats;
		unsigned long file_size_bytes;
		unsigned int io_size;
		test_t test_type;
		string test_file;
		int do_fsync;
		char *buf;
		struct time_stats stats;

		std::list<uint64_t> io_list;
        std::list<uint8_t> op_list;

		pthread_cond_t cv;
		pthread_mutex_t cv_mutex;

		void prepare(void);
		void cleanup(void);
		void delete_file(void);

		void do_read(void);
		void do_write(void);

		// Thread entry point.
		void Run(void);

		// util methods
		static unsigned long str_to_size(char* str);
		static test_t get_test_type(char *);
		static test_mode_t get_test_mode(char *);
		static void hexdump(void *mem, unsigned int len);
		static void show_usage(const char *prog);
};

io_bench::io_bench(int _id, unsigned long _file_size_bytes, 
		unsigned int _io_size, test_t _test_type)
	: id(_id), 
	file_size_bytes(_file_size_bytes), 
	io_size(_io_size), 
	test_type(_test_type)
{
	test_file.assign(test_dir_prefix);
	//test_file += "/file" + std::to_string(dev_id) + "-" + std::to_string(id) + std::to_string(getpid());
	//test_file += "/file." + std::to_string(id) + "." + std::to_string(dev_id);

	//for test fio, create files with names likes fio create for fio read
	//test_file += "/read." + std::to_string(id) + "." + std::to_string(dev_id);
	
	//test_file +="/file"+  std::to_string(id+1) + ".txt";
	test_file +="/file.0000000"+  std::to_string(id);
	per_thread_stats = 1;
}

#define handle_error_en(en, msg) \
	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

void io_bench::prepare(void)
{
	//printf("prepare\n");
	int ret, s;
	cpu_set_t cpuset;

#ifdef USERFS
	if(flag==0){
		init_fs();
		flag++;
	}
	//sleep(1);
	do_fsync = 0;
#else
	do_fsync = 1;
#endif
	struct timespec ts_start,ts_end;

	pthread_mutex_init(&cv_mutex, NULL);
	pthread_cond_init(&cv, NULL);

	//printf("mkdir\n");
	ret = mkdir(test_dir_prefix, 0777);

	if (ret < 0 && errno != EEXIST) { 
		perror("mkdir\n");
		exit(-1);
	}

	
#ifdef ODIRECT
	ret = posix_memalign((void **)&buf, 4096, BUF_SIZE);
	if (ret != 0)
		err(1, "posix_memalign");
#else
	buf = new char[(4 << 20)];
#endif

	char filename[256];
	strcpy(filename,test_file.c_str());

	if (test_type == SEQ_READ || test_type == RAND_READ) {
		for(unsigned long i = 0; i < BUF_SIZE; i++)
			buf[i] = 1;

	clock_gettime(CLOCK_REALTIME, &ts_start);
#ifdef ODIRECT
		if ((fd = userfs_posix_open(filename, O_RDWR| O_DIRECT, 0666)) < 0){
			//printf("O_DIRECT fd %d\n", fd);
		//if ((fd = userfs_posix_open(filename, O_RDWR| O_DIRECT, 0666)) < 0)
#else
		if ((fd = userfs_posix_open(test_file.c_str(), O_RDWR, 0666)) < 0){
			//printf("fd %d\n", fd);
#endif
			err(1, "open");
		}
		clock_gettime(CLOCK_REALTIME, &ts_end);
		printf("open_getstarttime : tv_sec=%ld, tv_nsec=%ld\n", ts_start.tv_sec, ts_start.tv_nsec);
        printf("open_getendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
        printf("open time %d\n", ts_end.tv_nsec-ts_start.tv_nsec);

		//printf("fd %d\n", fd);
	} else {
		//printf("else if \n");
		for (unsigned long i = 0; i < BUF_SIZE; i++) 
			buf[i] = '0' + (i % 10);
		//printf("open %s\n",test_file.c_str());
		clock_gettime(CLOCK_REALTIME, &ts_start);
#ifdef ODIRECT
		//printf("ODIRECT\n");
		fd = userfs_posix_open(filename, O_RDWR | O_CREAT| O_TRUNC | O_DIRECT,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		//printf("fd1 is %d\n", fd);
#else
		//printf("else ODIRECT\n");
		fd = open(test_file.c_str(), O_RDWR | O_CREAT| O_TRUNC,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
		clock_gettime(CLOCK_REALTIME, &ts_end);
        printf("create_getstarttime : tv_sec=%ld, tv_nsec=%ld\n", ts_start.tv_sec, ts_start.tv_nsec);
        printf("create_getendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
        printf("create time %d\n", ts_end.tv_nsec-ts_start.tv_nsec);

		if (fd < 0) {
			err(1, "open");
		}
	}
	printf("fd is %d\n", fd);
	//printf("fd2 is %d\n", fd);
	/**
	 * If its random write and FS, we preallocate the file so we can do
	 * random writes
	 */
	/*
	if (test_type == RAND_WRITE || test_type == ZIPF_WRITE) { 
		//fallocate(fd, 0, 0, file_size_bytes);
		cout << "allocate file" << endl;

		test_t test_type_back = test_type;

		test_type = SEQ_WRITE;
		this->do_write();

		test_type = test_type_back;

		lseek(fd, 0, SEEK_SET);
	}
	*/
	//printf("RAND_WRITE\n");
	if (test_type == RAND_WRITE || test_type == RAND_READ) {
		std::random_device rd;
		std::mt19937 mt(rd());
		//std::mt19937 mt;
		std::uniform_int_distribution<uint64_t> dist(0, file_size_bytes - 4096);
		//printf("file_size_bytes / io_size %d\n", file_size_bytes / io_size);
		for (uint64_t i = 0; i < file_size_bytes / io_size; i++) {
			//printf("i =%d\n",i);
			io_list.push_back(dist(mt));
			//io_list.push_back(ALIGN((dist(mt)), (4 << 10)));
		}
	} else if (test_type == ZIPF_WRITE || test_type == ZIPF_READ || test_type == ZIPF_MIX) {
	}

	/*
	for(auto it : io_list)
		cout << it << endl;
	*/
	//printf("prepare %d\n",fd);
}
int mark=0;
void io_bench::do_write(void)
{
	
	int ret;
	unsigned long random_range;
	unsigned long cur_offset;
	struct timespec ts_start,ts_end;
	/*
	if(mark%1==0){
		file_size_bytes=4194304;
	}else{
		file_size_bytes=1048576;
	}
	mark++;
	*/
	//printf("do_write %d\n",file_size_bytes);
	random_range = file_size_bytes / io_size;

	if (1) {
		time_stats_init(&stats, 1);
		time_stats_start(&stats);
	}
	//printf("test_type == SEQ_WRITE file_size_bytes %d\n", file_size_bytes);
	//printf("file_size_bytes %d\n", file_size_bytes);
	if (test_type == SEQ_WRITE || test_type == SEQ_WRITE_READ) {
		for (unsigned long i = 0; i < file_size_bytes; i += io_size) {
			
			if (i + io_size > file_size_bytes)
				io_size = file_size_bytes - i;
			else
				io_size = io_size;

#ifdef VERIFY
			printf("VERIFY\n");
			for (int j = 0; j < io_size; j++) {
				cur_offset = i + j;	
				buf[j] = '0' + (cur_offset % 10);
			}
#endif		
			//printf("fd is %d\n", fd);
			clock_gettime(CLOCK_REALTIME, &ts_start);
			ret = userfs_posix_write(fd, buf, io_size);
			clock_gettime(CLOCK_REALTIME, &ts_end);
            //printf("write_getstarttime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_start.tv_sec, ts_start.tv_nsec,io_size);
            //printf("write_getendtime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_end.tv_sec, ts_end.tv_nsec,io_size);
            //printf("write time %d\n", ts_end.tv_nsec-ts_start.tv_nsec);
			if (ret != io_size) {
				//printf("write request %u received len %d\n", io_size, ret);
				errx(1, "write");
			}
		}
	} else if (test_type == RAND_WRITE || test_type == ZIPF_WRITE) {
		unsigned int _io_size = io_size;
		unsigned long flag = ~(io_size -1);
		for (auto it : io_list) {
			/*
			if (it + io_size > file_size_bytes) {
				_io_size = file_size_bytes - it;
				cout << _io_size << endl;
			} else
				_io_size = io_size;
			*/
			//printf("off %d, io_size %d\n", it, _io_size);
			//printf("it %d, flag %d, it & flag %d\n", it,flag,it & flag);
			it = it & flag;
			//clock_gettime(CLOCK_REALTIME, &ts_start);
			userfs_posix_lseek(fd, it, SEEK_SET);
			ret = userfs_posix_write(fd, buf, _io_size);
			//clock_gettime(CLOCK_REALTIME, &ts_end);
            //printf("write_getstarttime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_start.tv_sec, ts_start.tv_nsec,io_size);
            //printf("write_getendtime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_end.tv_sec, ts_end.tv_nsec,io_size);
            //printf("rand write time %d\n", ts_end.tv_nsec-ts_start.tv_nsec);
			if (ret != _io_size) {
				printf("write request %u received len %d\n",
						_io_size, ret);
				errx(1, "write");
			}
		}
	}
    else if (test_type == ZIPF_MIX) {
		unsigned int _io_size = io_size;
		int ret;
        std::list<uint8_t>::iterator op_it = op_list.begin();
		for (auto it : io_list) {
			lseek(fd, it, SEEK_SET);
            //read
            if (*op_it == 0) {
                ret = read(fd, buf, io_size);
				if (ret < 0) 
					errx(1, "read");
            }
            //write
            else {
                ret = write(fd, buf, _io_size);
    			if (ret != _io_size) {
    				printf("write request %u received len %d\n",
    						_io_size, ret);
    				errx(1, "write");
    			}
            }	
		    ++op_it;
		}
    }

	if (do_fsync) {
		printf("do_sync\n");
		fsync(fd);
	}

	if (1) {
		time_stats_stop(&stats);
		printf("write fd is %d\n", fd);
		time_stats_print(&stats, (char *)"1---------------");

		printf("Throughput: %3.3f MB\n",(float)(file_size_bytes)
				/ (1024.0 * 1024.0 * (float) time_stats_get_avg(&stats)));
	}
	printf("end write\n");
	return ;
}

void io_bench::do_read(void)
{
	printf("do_read\n");
	int ret;
	unsigned long cur_offset;
	struct timespec ts_start,ts_end;


	clock_gettime(CLOCK_REALTIME, &ts_start);
	printf("read_getstarttime : tv_sec=%ld, tv_nsec=%ld\n", ts_start.tv_sec, ts_start.tv_nsec);
	if (per_thread_stats) {
		time_stats_init(&stats, 1);
		time_stats_start(&stats);
	}
	int64_t off=0;
	if (test_type == SEQ_READ || test_type == SEQ_WRITE_READ) {
		userfs_posix_lseek(fd, off, SEEK_SET);
		/*
		char *readbuf;
		ret = posix_memalign((void **)&readbuf, 4096, BUF_SIZE);
		if (ret != 0)
			err(1, "posix_memalign");
		*/
		for (unsigned long i = 0; i < file_size_bytes ; i += io_size) {
			if (i + io_size > file_size_bytes)
				io_size = file_size_bytes - i;
			else
				io_size = io_size;
#ifdef VERIFY
			memset(buf, 0, io_size);

#endif
			//ret = read(fd, buf, io_size);
			//printf("io_size %d\n", io_size);
			//struct timespec start, end;
			//clock_gettime(CLOCK_REALTIME, &start);
			//printf("readbuf buf %lu\n",buf);
			//clock_gettime(CLOCK_REALTIME, &ts_start);
			//printf("read_getstarttime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_start.tv_sec, ts_start.tv_nsec,io_size);
			ret = userfs_posix_read(fd, buf, io_size);
			//clock_gettime(CLOCK_REALTIME, &ts_end);
            //printf("read_getstarttime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_start.tv_sec, ts_start.tv_nsec,io_size);
            //printf("read_getendtime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_end.tv_sec, ts_end.tv_nsec,io_size);
            //printf("userfs_posix_read time %d\n", ts_end.tv_nsec-ts_start.tv_nsec);

			//ret = userfs_posix_read(fd, readbuf, io_size);
			//clock_gettime(CLOCK_REALTIME, &end);
		
   			//printf("userfs_posix_read use time %d nsec\n", end.tv_nsec - start.tv_nsec);
#if 1
			if (ret != io_size) {
				printf("read size mismatch: return %d, request %lu\n",
						ret, io_size);
			}
#endif
#ifdef VERIFY
			// verify buffer
			printf("off %d read buf %s\n",i, buf);
			for (unsigned int j = 0; j < io_size; j++) {
				cur_offset = i + j;
				if (buf[j] != '0' + (cur_offset % 10)) {
					//hexdump(buf + j, 256);
					printf("read data mismatch at %lu\n", i);
					printf("expected %c read %c\n", (int)('0' + (cur_offset % 10)), buf[j]);
					printf("read buf %s\n", buf);
					exit(-1);
					break;
				}
			}
#endif
		}
	} else if (test_type == RAND_READ || test_type == ZIPF_READ) {
		unsigned long flag = ~(io_size -1);

		for (auto it : io_list) {
		/*
			if (it + io_size > file_size_bytes)
				io_size = file_size_bytes - it;
			else
				io_size = io_size;
		*/
			it = it & flag;
			//clock_gettime(CLOCK_REALTIME, &ts_start);
			userfs_posix_lseek(fd, it, SEEK_SET);
			ret = userfs_posix_read(fd, buf, io_size);
			//clock_gettime(CLOCK_REALTIME, &ts_end);
           // printf("read_getstarttime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_start.tv_sec, ts_start.tv_nsec,io_size);
            //printf("read_getendtime : tv_sec=%ld, tv_nsec=%ld,io_size %d\n", ts_end.tv_sec, ts_end.tv_nsec,io_size);
            //printf("rand read time %d\n", ts_end.tv_nsec-ts_start.tv_nsec);

			if (ret != io_size) {
				printf("write request %u received len %d\n", io_size, ret);
				errx(1, "write");
			}
			//ret = pread(fd, buf, io_size, it);
		}
	}

#if 0
	for (unsigned long i = 0; i < file_size_bytes; i++) {
		int bytes_read = read(fd, buf+i, io_size + 100);

		if (bytes_read != io_size) {
			printf("read too far: length %d\n", bytes_read);
		}
	}
#endif

	clock_gettime(CLOCK_REALTIME, &ts_end);
    printf("read_getendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);

	if (per_thread_stats)  {
		time_stats_stop(&stats);
		time_stats_print(&stats, (char *)"---------------");

		printf("%f\n", (float) time_stats_get_avg(&stats));

		printf("Throughput: %3.3f MB\n",(float)((file_size_bytes) >> 20)
				/ (float) time_stats_get_avg(&stats));
	}

	return ;
}

int drop_cache =1;
void io_bench::Run(void)
{
	printf("Run\n");
	cout << "thread " << id << " start - ";
	cout << "file: " << test_file << endl;
	cout <<"test_type： "<<test_type<<endl;
	if (test_type == SEQ_READ || test_type == RAND_READ || test_type == ZIPF_READ){
		cout<<"read"<<endl;
		//struct timeval start, end;
		//gettimeofday(&start, NULL);
		this->do_read();
		//gettimeofday(&end, NULL);
		
   		//printf("read use time %d usec\n", end.tv_usec - start.tv_usec);
	}
	else {
		cout<<"do_write"<<endl;
		struct timeval start, end;
		gettimeofday(&start, NULL);
		this->do_write();
		gettimeofday(&end, NULL);
		
   		printf("write use time %d usec\n", (end.tv_sec - start.tv_sec) *1000000 +(end.tv_usec - start.tv_usec));
	}
	//system("echo 3 > /proc/sys/vm/drop_caches");
	pthread_barrier_wait(&b);
	
	if (test_type == SEQ_WRITE_READ) {
		if(__sync_bool_compare_and_swap(&drop_cache, 1 ,2)){
			printf("drop_cache %d\n", drop_cache);
			system("echo 3 > /proc/sys/vm/drop_caches");
		}
		pthread_barrier_wait(&b);
	

		lseek(fd, 0, SEEK_SET);
		printf("do_read\n");
		struct timeval start, end;
		gettimeofday(&start, NULL);
		printf("do_read start sec %d, usec %d\n", start.tv_sec, start.tv_usec);
		this->do_read();

		gettimeofday(&end, NULL);
		printf("do_read end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
   		printf("read use time %d usec\n", (end.tv_sec - start.tv_sec) *1000000 +(end.tv_usec - start.tv_usec));
	}

	//pthread_mutex_lock(&cv_mutex);
	//pthread_cond_signal(&cv);
	pthread_mutex_unlock(&cv_mutex);
	//printf("end Run\n");
	return;
}

void io_bench::cleanup(void)
{
	printf("cleanup %d\n",fd);
	userfs_posix_close(fd);
	//printf("cleanup1\n");
#if 0
	if (test_type == SEQ_READ || test_type == RAND_READ) {
		// Read data integrity check.
		for (unsigned long i = 0; i < file_size_bytes; i++) {
			if (buf[i] != '0' + (i % 10)) {
				hexdump(buf + i, 256);
				printf("read data mismatch at %lu\n", i);
				printf("expected %c read %c\n", (int)('0' + (i % 10)), buf[i]);
				exit(-1);
			}
		}

		printf("Read data matches\n");
	}
#endif
	//printf("cleanup2\n");
#ifdef ODIRECT
	//free(buf);
#else
	//delete buf;
#endif
	//printf("cleanup3\n");
}

void io_bench::delete_file(void)
{
	unlink(test_file.c_str());
	return ;
}

unsigned long io_bench::str_to_size(char* str)
{
	/* magnitude is last character of size */
	char size_magnitude = str[strlen(str)-1];
	/* erase magnitude char */
	str[strlen(str)-1] = 0;
	unsigned long file_size_bytes = strtoull(str, NULL, 0);
	switch(size_magnitude) {
		case 'g':
		case 'G':
			file_size_bytes *= 1024;
		case 'm':
		case 'M':
			file_size_bytes *= 1024;
		case '\0':
		case 'k':
		case 'K':
			file_size_bytes *= 1024;
			break;
		case 'p':
		case 'P':
			file_size_bytes *= 4;
			break;
		case 'b':
		case 'B':
         break;
		default:
			std::cout << "incorrect size format " << str << endl;
			break;
	}
	return file_size_bytes;
}

test_t io_bench::get_test_type(char *test_type)
{
	/**
	 * Check the mode to bench: read or write and type
	 */
	if (!strcmp(test_type, "sr")){
		return SEQ_READ;
	}
	else if (!strcmp(test_type, "sw")) {
		return SEQ_WRITE;
	}
	else if (!strcmp(test_type, "wr")) {
		return SEQ_WRITE_READ;
	}
	else if (!strcmp(test_type, "rw")) {
		return RAND_WRITE;
	}
	else if (!strcmp(test_type, "rr")) {
		return RAND_READ;
	}
	else if (!strcmp(test_type, "zw")) {
		return ZIPF_WRITE;
	}
	else if (!strcmp(test_type, "zr")) {
		return ZIPF_READ;
	}
    else if (!strcmp(test_type, "zm")) {
        return ZIPF_MIX;
    }
	else { 
		show_usage("iobench");
		cerr << "unsupported test type" << test_type << endl;
		exit(-1);
	}
}

#define HEXDUMP_COLS 8
void io_bench::hexdump(void *mem, unsigned int len)
{
	unsigned int i, j;

	for(i = 0; i < len + ((len % HEXDUMP_COLS) ?
				(HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++) {
		/* print offset */
		if(i % HEXDUMP_COLS == 0) {
			printf("0x%06x: ", i);
		}

		/* print hex data */
		if(i < len) {
			printf("%02x ", 0xFF & ((char*)mem)[i]);
		} else {/* end of block, just aligning for ASCII dump */
			printf("	");
		}

		/* print ASCII dump */
		if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1)) {
			for(j = i - (HEXDUMP_COLS - 1); j <= i; j++) {
				if(j >= len) { /* end of block, not really printing */
					printf(" ");
				} else if(isprint(((char*)mem)[j])) { /* printable char */
					printf("%c",(0xFF & ((char*)mem)[j]));
				} else {/* other char */
					printf(".");
				}
			}
			printf("\n");
		}
	}
}

void io_bench::show_usage(const char *prog)
{
	std::cerr << "usage: " << prog
		<< " [-d <directory>] <wr/sr/sw/rr/rw/zr/zw/zm>"
		<< " <size: X{G,M,K,P}, eg: 100M> <IO size, e.g.: 4K> <# of thread>"
      << endl;
}

/* Returns new argc */
static int adjust_args(int i, char *argv[], int argc, unsigned del)
{
   if (i >= 0) {
      for (int j = i + del; j < argc; j++, i++)
         argv[i] = argv[j];
      argv[i] = NULL;
      return argc - del;
   }
   return argc;
}

int process_opt_args(int argc, char *argv[])
{
   int dash_d = -1;

   for (int i = 0; i < argc; i++) {
      if (strncmp("-d", argv[i], 2) == 0) {
         test_dir_prefix = argv[i+1];
         dash_d = i;
      }
   }

   return adjust_args(dash_d, argv, argc, 2);
}

int main(int argc, char *argv[])
{
	int n_threads, i;
	std::vector<io_bench *> io_workers;
	unsigned long file_size_bytes;
	unsigned int io_size = 0;
	struct time_stats main_stats, total_stats;
	const char *device_id;
	char zipf_file_name[100];

	device_id = getenv("DEV_ID");

	if (device_id) 
		dev_id = atoi(device_id);
	else
		dev_id = 0;

	argc = process_opt_args(argc, argv);
	if (argc < 5) {
		io_bench::show_usage(argv[0]);
		exit(-1);
	}

	n_threads = std::stoi(argv[4]);

	file_size_bytes = io_bench::str_to_size(argv[2]);
	io_size = io_bench::str_to_size(argv[3]);

	std::cout << "Total file size: " << file_size_bytes << "B" << endl
		<< "io size: " << io_size << "B" << endl
		<< "# of thread: " << n_threads << endl;

	pthread_barrier_init(&b,NULL,n_threads);

	for (i = 0; i < n_threads; i++) {
		io_workers.push_back(new io_bench(i, 
					file_size_bytes,
					io_size,
					io_bench::get_test_type(argv[1])));

	}
	
	
	//std::cout <<"io_bench"<<endl;
	time_stats_init(&main_stats, 1);
	time_stats_init(&total_stats, 1);

	//time_stats_start(&total_stats);
	//std::cout <<"io_bench1"<<endl;
#define N_REPEAT 1
	//std::cout <<"io_bench2"<<endl;
	for (int i = 0; i < N_REPEAT ; i++) {
		//std::cout <<"io_bench3"<<endl;
		//printf("main2\n");
		for (auto it : io_workers) {
			//std::cout <<"io_bench4"<<endl;
			it->prepare();
			pthread_mutex_lock(&it->cv_mutex);
			it->per_thread_stats = 0;
		}
		//std::cout <<"io_bench5"<<endl;
		time_stats_start(&total_stats);
		time_stats_start(&main_stats);
		struct timeval start;
		gettimeofday(&start, NULL);
		printf("start time %d sec, %d usec\n",start.tv_sec, start.tv_usec);
		for (auto it : io_workers) {
			it->Start();
			//std::cout <<"it->Start()"<<endl;
			//pthread_mutex_lock(&it->cv_mutex);
		}
		for (auto it : io_workers) {
			pthread_mutex_lock(&it->cv_mutex);
			pthread_mutex_unlock(&it->cv_mutex);
		}

		printf("main2.0\n");
		for (auto it : io_workers) 
			it->cleanup();

		/*
		for (auto it : io_workers) 
			it->delete_file();
		*/

	}
	//printf("main3\n");
	

	for (auto it : io_workers) 
		it->Join();
	//printf("main4\n");
	

	//for (auto it : io_workers) 
	//		it->cleanup();
	time_stats_stop(&main_stats);
#ifdef USERFS
	//printf("call shutdown_fs\n");
	//shutdown_fs();
#endif

	shutdown_fs();
	//printf("main4\n");
	
	time_stats_stop(&total_stats);

	time_stats_print(&main_stats, (char *)"--------------- stats");
	printf("main5\n");
	printf("Aggregated throughput: %3.3f MB/s\n",
			((float)n_threads * (float)((file_size_bytes) >> 20))
			/ (float) time_stats_get_avg(&main_stats));
	printf("--------------------------------------------\n");

	time_stats_print(&total_stats, (char *)"----------- total stats");
	
	printf("Total Aggregated throughput: %3.3f MB/s\n",
                        ((float)n_threads * (float)((file_size_bytes) >> 20))
                        / (float) time_stats_get_avg(&total_stats));
        printf("--------------------------------------------\n");

	fflush(stdout);
	fflush(stderr);

	return 0;
}