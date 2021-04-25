#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "userfs/userfs_user.h"
#include "global/global.h"
#include "concurrency/synchronization.h"
#include "concurrency/thread.h"
#include "filesystem/fs.h"
#include "io/block_io.h"
#include "filesystem/file.h"
//#include "log/log.h"
#include "userfs/userfs_interface.h"
#include "ds/bitmap.h"
#include "ds/blktree.h"
#include "filesystem/slru.h"

#define _min(a, b) ({\
		__typeof__(a) _a = a;\
		__typeof__(b) _b = b;\
		_a < _b ? _a : _b; })

#define THREAD_NUM 64

int log_fd = 0;
int shm_fd = 0;

int bitmapnum = 0;
uint8_t allodev=0;

struct disk_superblock *disk_sb;
struct super_block *sb;
ncx_slab_pool_t *userfs_slab_pool;
ncx_slab_pool_t *userfs_slab_pool_shared;
uint8_t *shm_base;
uint8_t shm_slab_index = 0;

uint8_t g_log_dev = 0;
uint8_t g_ssd_dev = 0;
uint8_t g_hdd_dev = 0;

uint8_t initialized = 0;

// statistics
uint8_t enable_perf_stats;

struct lru g_fcache_head;

pthread_rwlock_t *icache_rwlock;
pthread_rwlock_t *dcache_rwlock;
pthread_rwlock_t *dlookup_rwlock;
pthread_rwlock_t *invalidate_rwlock;
pthread_rwlock_t *g_fcache_rwlock;
pthread_mutex_t *bitmap_lock;
pthread_mutex_t *nblock_lock;
pthread_mutex_t *sblock_lock;
pthread_mutex_t *allossd_lock;
pthread_mutex_t *allossd1_lock;
pthread_mutex_t *allossd2_lock;
pthread_mutex_t *allossd3_lock;
pthread_mutex_t *allonvm_lock;
pthread_mutex_t *applog_lock;
pthread_mutex_t *sblock_lock;
pthread_rwlock_t *shm_slab_rwlock; 
pthread_rwlock_t *shm_lru_rwlock; 

struct inode *inode_hash;
struct dirent_block *dirent_hash;
struct dlookup_data *dlookup_hash;


struct threadpool_t *writethdpool;
struct threadpool_t *readthdpool;
struct threadpool_t *committhdpool;

#define QUEUE_SIZE 65536
uint8_t nvmnum =1;
uint8_t ssdnum =3;

threadpool *writepool[5];
threadpool *readpool[5];


libfs_stat_t g_perf_stats;
float clock_speed_mhz;



uint8_t writethdnum;
uint8_t readthdnum;
uint8_t committhdnum;

pthread_mutex_t *fileno_lock;

char logname[256]="/mnt/pmem_emul/.log";

static inline float tsc_to_ms(uint64_t tsc) 
{
	return (float)tsc / (clock_speed_mhz * 1000.0);
}


/*
int bitmapnum = 0;

static uint32_t find_inode_bitmap(){
	//printf("find_inode_bitmap function %lu, %lu, %lu\n",logaddr,logstart,logend);
	int i;
	uint32_t ino;
	printf("0inode bitmapnum is %d\n", bitmapnum);
	pthread_mutex_lock(bitmap_lock);
	for(i =bitmapnum;i<inonum;i++){
		printf("fsinode_table[%d] %d\n",i,fsinode_table[i]);
		if(fsinode_table[i]!=0){
			ino= fsinode_table[i];
			fsinode_table[i]=0;
			//ino=ino>>inode_shift;
			printf("inode ino is %d\n", ino);
			bitmapnum=i+1;
			printf("1inode bitmapnum is %d\n", bitmapnum);
			pthread_mutex_unlock(bitmap_lock);
			return ino;
		}
	}
	pthread_mutex_unlock(bitmap_lock);
	return 0;
}

static uint32_t put_inode_bitmap(uint32_t ino){
	printf("put_inode_bitmap function\n");
	int i;
	//uint32_t ino;
	pthread_mutex_lock(bitmap_lock);
	printf("00inode bitmapnum is %d\n", bitmapnum);
	for(i =bitmapnum;i>=0;i--){
		printf("fsinode_table[%d] %d\n",i,fsinode_table[i]);
		if(fsinode_table[i]==0){
			//ino= fsinode_table[i];
			fsinode_table[i]=ino;
			//ino=ino>>inode_shift;
			bitmapnum=i;
			printf("11inode bitmapnum is %d\n", bitmapnum);
			pthread_mutex_unlock(bitmap_lock);
			return ino;
		}
	}
	pthread_mutex_unlock(bitmap_lock);
	return 0;
}
*/

void show_libfs_stats(void)
{
	printf("\n");
	printf("----------------------- libfs statistics\n");
	// For some reason, floating point operation causes segfault in filebench 
	// worker thread.
	printf("wait on digest    : %.3f ms\n", tsc_to_ms(g_perf_stats.digest_wait_tsc));
	printf("inode allocation  : %.3f ms\n", tsc_to_ms(g_perf_stats.ialloc_tsc));
	printf("bcache search     : %.3f ms\n", tsc_to_ms(g_perf_stats.bcache_search_tsc));
	printf("search l0 tree    : %.3f ms\n", tsc_to_ms(g_perf_stats.l0_search_tsc));
	printf("search lsm tree   : %.3f ms\n", tsc_to_ms(g_perf_stats.tree_search_tsc));
	printf("log commit        : %.3f ms\n", tsc_to_ms(g_perf_stats.log_commit_tsc));
	printf("  log writes      : %.3f ms\n", tsc_to_ms(g_perf_stats.log_write_tsc));
	printf("  loghdr writes   : %.3f ms\n", tsc_to_ms(g_perf_stats.loghdr_write_tsc));
	printf("read data blocks  : %.3f ms\n", tsc_to_ms(g_perf_stats.read_data_tsc));
	printf("directory search  : %.3f ms\n", tsc_to_ms(g_perf_stats.dir_search_tsc));
	printf("temp_debug        : %.3f ms\n", tsc_to_ms(g_perf_stats.tmp_tsc));
	/*
	printf("wait on digest  (tsc)  : %lu \n", g_perf_stats.digest_wait_tsc);
	printf("inode allocation (tsc) : %lu \n", g_perf_stats.ialloc_tsc);
	printf("bcache search (tsc)    : %lu \n", g_perf_stats.bcache_search_tsc);
	printf("search l0 tree  (tsc)  : %lu \n", g_perf_stats.l0_search_tsc);
	printf("search lsm tree (tsc)  : %lu \n", g_perf_stats.tree_search_tsc);
	printf("log commit (tsc)       : %lu \n", g_perf_stats.log_commit_tsc);
	printf("  log writes (tsc)     : %lu \n", g_perf_stats.log_write_tsc);
	printf("  loghdr writes (tsc)  : %lu \n", g_perf_stats.loghdr_write_tsc);
	printf("read data blocks (tsc) : %lu \n", g_perf_stats.read_data_tsc);
	printf("directory search (tsc) : %lu \n", g_perf_stats.dir_search_tsc);
	printf("temp_debug (tsc)       : %lu \n", g_perf_stats.tmp_tsc);
	*/
#if 0
	printf("wait on digest (nr)   : %lu \n", g_perf_stats.digest_wait_nr);
	printf("search lsm tree (nr)  : %lu \n", g_perf_stats.tree_search_nr);
	printf("log writes (nr)       : %lu \n", g_perf_stats.log_write_nr);
	printf("read data blocks (nr) : %lu \n", g_perf_stats.read_data_nr);
	printf("directory search hit  (nr) : %lu \n", g_perf_stats.dir_search_nr_hit);
	printf("directory search miss (nr) : %lu \n", g_perf_stats.dir_search_nr_miss);
	printf("directory search notfound (nr) : %lu \n", g_perf_stats.dir_search_nr_notfound);
#endif
	printf("--------------------------------------\n");
}

void synclog(void){
	int ret;
	struct list_head* head = &(inodelru->lru_nodes);
	struct list_head* p = NULL;

	uint32_t inode[1024];
	int i=0;
	printf("+++++++++++++++++++++++++\n");
	list_for_each(p,head){
		lru_node_t* node = list_entry(p, lru_node_t, lru_list);
		printf("key is %s %d\n",node->key,node->ino);
		inode[i++]=node->ino;
	}
	printf("+++++++++++++++++++\n");
	//ret = syscall(330, inode,i);
	//ret = syscall(330, inode, i);
	
	printf("logstart %lu, logbase %lu, logoffset %ld\n",logstart,logbase,logoffset );
	syscall(329,logname,logstart-logbase,logstart + logoffset-logbase);
	logstart=logbase+ logoffset;
	logsoff=logoffset;


}



void shutdown_fs(void)
{
	printf("shutdown_fs\n");
	int ret;
	int _enable_perf_stats = enable_perf_stats;

	struct timeval end;
    gettimeofday(&end, NULL);
    //printf("shutdown_fs end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
#ifdef CONCURRENT
    //threadpool_destroy(writethdpool,1);
    //if(thpool_wait(writepool[1])&&thpool_wait(writepool[2])&&thpool_wait(writepool[3])&&thpool_wait(writepool[4]))
    //	;
    //thpool_wait(writepool[1]);
    thpool_wait(writepool[1]);
    //thpool_wait(writepool[3]);
    //thpool_wait(writepool[4]);
    //while(thpool_joblen(writepool[1])>0||thpool_joblen(writepool[2])>0||thpool_joblen(writepool[3])>0||thpool_joblen(writepool[4])>0){
    //printf("writepool->queue_size 1: %d, 2: %d, 3: %d, 4: %d\n", thpool_joblen(writepool[1]),thpool_joblen(writepool[2]),thpool_joblen(writepool[3]),thpool_joblen(writepool[4]));
	//}
/*
    while(threadpool_queuesize(writepool[1])>0||threadpool_queuesize(writepool[2])>0||threadpool_queuesize(writepool[3])>0||threadpool_queuesize(writepool[4])>0){
    	printf("writepool->queue_size 1: %d, 2: %d, 3: %d, 4: %d\n", threadpool_queuesize(writepool[1]),threadpool_queuesize(writepool[2]),threadpool_queuesize(writepool[3]),threadpool_queuesize(writepool[4]));
    	usleep(1);
    }
*/    
#endif
    gettimeofday(&end, NULL);
    //printf("shutdown_fs end sec %d, usec %d\n", end.tv_sec, end.tv_usec);

   	//device_commit(1);
   	//gettimeofday(&end, NULL);
   	//printf("device_commit1 end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
   	device_commit(1);
   	//deviceexit(2);
	//gettimeofday(&end, NULL);
   	//printf("device_commit2 end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
   	//device_commit(3);
   	//gettimeofday(&end, NULL);
   	//printf("device_commit3 end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
   	//device_commit(4);
    //devicecommit();
/*	
	pthread_t ntid;
	int err = pthread_create(&ntid,NULL, devicecommit, NULL);
	if(err!=0){
		printf("create thread failed:%s\n",strerror(err));
		exit(-1);
	}else{
		printf("device_commit\n");
	}
*/
	
//	threadpool_destroy(readthdpool,1);
	//printf("threadpool_destroy\n");
	//threadpool_destroy(committhdpool,1);

	gettimeofday(&end, NULL);
        printf("shutdown_fs end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
	if (!initialized)
		return ;

	fflush(stdout);
	fflush(stderr);


	

	enable_perf_stats = 0;

	//shutdown_log();

	//syscall(submit bitmap)
	//undo syscall(submitlog);

	enable_perf_stats = _enable_perf_stats;

	if (enable_perf_stats) 
		show_libfs_stats();

	//struct list_head *head = &(inodelru->list);
	/*
	struct list_head* head = &(inodelru->lru_nodes);
	struct list_head* p = NULL;

	uint32_t inode[1024];
	int i=0;

	list_for_each(p,head){
		lru_node_t* node = list_entry(p, lru_node_t, lru_list);
		printf("key is %s %d\n",node->key,node->ino);
		inode[i++]=node->ino;
	}
	printf("---------------------\n");
*/

/*
	struct inodenode *in;
	uint32_t inode[1024];
	int i=0;
	list_for_each_entry(in,head,list){
		printf("inode->ino is %d\n",in->ino);
		inode[i++]=in->ino;
	}
	*/
	
	//ret = syscall(330, inode, i);
	
	//msync(logstart, logspace, MS_SYNC);
	//sleep(1);
/*
	append_addrinfo_log();

	ret=munmap(logstart,logspace);
	unsigned long endaddr = nblock_table[0][1]<<page_shift;
	munmap(mmapstart, endaddr); //destroy map memory
   	//close(pmemfd);   //close file

	if (ret == -1)
		panic("cannot unmap shared memory\n");
	//printf("logstart %lu, logbase %lu, logoffset %ld\n",logstart,logbase,logoffset );
	
	struct timespec ts_start,ts_end;
	clock_gettime(CLOCK_REALTIME, &ts_start);
	syscall(337,logname,logstart-logbase,logstart + logoffset-logbase);
	clock_gettime(CLOCK_REALTIME, &ts_end);
	//printf("synclog : tv_sec=%ld, tv_nsec=%ld\n", ts_start.tv_sec, ts_start.tv_nsec);
	//printf("synclog : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
   	//printf("synclog time %d\n", (ts_end.tv_sec-ts_start.tv_sec)*1000000000+ts_end.tv_nsec-ts_start.tv_nsec);
	logstart=logbase+ logoffset;
	logsoff=logoffset;
*/
	
//	device_exit();

	//syscall(329,logname,0,4096); //for test
	/*
	ret = munmap(userfs_slab_pool_shared, SHM_SIZE);
	if (ret == -1)
		panic("cannot unmap shared memory\n");

	ret = close(shm_fd);
	if (ret == -1)
		panic("cannot close shared memory\n");
	*/
	//system("echo 3 > /proc/sys/vm/drop_caches");
   	//sleep(10);
   	gettimeofday(&end, NULL);
    printf("shutdown_fs end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
	return ;
}

#ifdef USE_SLAB
void userfs_slab_init(uint64_t pool_size) 
{
	uint8_t *pool_space;

	// MAP_SHARED is used to share memory in case of fork.
	pool_space = (uint8_t *)mmap(0, pool_size, PROT_READ|PROT_WRITE, 
			MAP_SHARED|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);

	userfs_assert(pool_space);

	if (madvise(pool_space, pool_size, MADV_HUGEPAGE) < 0) 
		panic("cannot do madvise for huge page\n");

	userfs_slab_pool = (ncx_slab_pool_t *)pool_space;
	userfs_slab_pool->addr = pool_space;
	userfs_slab_pool->min_shift = 3;
	userfs_slab_pool->end = pool_space + pool_size;

	ncx_slab_init(userfs_slab_pool);
}
#endif

void debug_init(void)
{
#ifdef USERFS_LOG
	log_fd = open(LOG_PATH, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
#endif
}

void shared_slab_init(uint8_t _shm_slab_index)
{
	/* TODO: make the following statment work */
	/* shared memory is used for 2 slab regions.
	 * At the beginning, The first region is used for allocating lru list.
	 * After libfs makes a digest request or lru update request, libfs must free 
	 * a current lru list and start build new one. Instead of iterating lru list, 
	 * Libfs reintialize slab to the second region and initialize head of lru.
	 * This is because kernel FS might be still absorbing the LRU list 
	 * in the first region.(kernel FS sends ack of digest and starts absoring 
	 * the LRU list to reduce digest wait time.)
	 * Likewise, the second region is toggle to the first region 
	 * when it needs to build a new list.
	 */
/*	typedef struct {
	size_t min_size;
	size_t min_shift;

	ncx_slab_page_t *pages;
	ncx_slab_page_t free;

	u_char *start;
	u_char *end;

	pthread_spinlock_t lock;

	void *addr;
} ncx_slab_pool_t;
*/
	//printf("shared_slab_init\n");
	userfs_slab_pool_shared = (ncx_slab_pool_t *)(shm_base + 4096);
	//printf("shared_slab_init1 %d\n",userfs_slab_pool_shared->addr);
	//printf("shared_slab_init1 %d\n",shm_base+4096);
	userfs_slab_pool_shared->addr = shm_base + 4096;
	//printf("shared_slab_init1 %d\n",shm_base);
	userfs_slab_pool_shared->addr = (shm_base + 4096) + _shm_slab_index * (SHM_SIZE / 2);
	//printf("shared_slab_init2\n");
	userfs_slab_pool_shared->min_shift = 3;
	//printf("shared_slab_init2\n");
	userfs_slab_pool_shared->end = userfs_slab_pool_shared->addr + (SHM_SIZE / 2);
	//printf("shared_slab_init2\n");
	ncx_slab_init(userfs_slab_pool_shared);
}

static void shared_memory_init(void)
{
	int ret;
	//printf("shared_memory_init\n");
	shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
	//printf("shared_memory_init %d\n",shm_fd);
	if (shm_fd == -1) 
		panic("cannot open shared memory\n");

	// the first 4096 is reserved for lru_head array.
	//#define SHM_SIZE (200 << 20)
	ret = ftruncate(shm_fd, SHM_SIZE + 4096);
    if(-1 == ret)
    {
        panic("ftruncate faile: ");
    }
	shm_base = (uint8_t *)mmap(SHM_START_ADDR, 
			SHM_SIZE + 4096, 
			PROT_READ | PROT_WRITE, 
			MAP_SHARED | MAP_FIXED, 
			shm_fd, 0);
	//printf("shmbase %d len %d\n", SHM_START_ADDR,SHM_SIZE+4096);
	if (shm_base == MAP_FAILED) 
		panic("cannot map shared memory\n");

	//printf("shared_memory_init1\n");
	shm_slab_index = 0;
	shared_slab_init(shm_slab_index);
	//printf("shared_memory_init2\n");
	bandwidth_consumption = (uint64_t *)shm_base;

	lru_heads = (struct list_head *)shm_base + 128;
	
	INIT_LIST_HEAD(&lru_heads[g_log_dev]);
}

static void cache_init(void)
{
	int i;
	
	//for (i = 1; i < g_n_devices + 1; i++) {
		inode_hash = NULL;
		dirent_hash = NULL;
		dlookup_hash = NULL;
	//}

	lru_hash = NULL;

	INIT_LIST_HEAD(&g_fcache_head.lru_head);
	g_fcache_head.n = 0;
}

static void locks_init(void)
{
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	icache_rwlock = (pthread_rwlock_t *)userfs_zalloc(sizeof(pthread_rwlock_t));
	dcache_rwlock = (pthread_rwlock_t *)userfs_zalloc(sizeof(pthread_rwlock_t));
	dlookup_rwlock = (pthread_rwlock_t *)userfs_zalloc(sizeof(pthread_rwlock_t));
	invalidate_rwlock = (pthread_rwlock_t *)userfs_zalloc(sizeof(pthread_rwlock_t));
	g_fcache_rwlock = (pthread_rwlock_t *)userfs_zalloc(sizeof(pthread_rwlock_t));
	bitmap_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	nblock_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	sblock_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	fileno_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	applog_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	allossd1_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	allossd2_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	allossd3_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	allossd_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	allonvm_lock = (pthread_mutex_t *)userfs_zalloc(sizeof(pthread_mutex_t));
	shm_slab_rwlock = (pthread_rwlock_t *)userfs_alloc(sizeof(pthread_rwlock_t));
	shm_lru_rwlock = (pthread_rwlock_t *)userfs_alloc(sizeof(pthread_rwlock_t));

	pthread_rwlock_init(icache_rwlock, &rwlattr);
	pthread_rwlock_init(dcache_rwlock, &rwlattr);
	pthread_rwlock_init(dlookup_rwlock, &rwlattr);
	pthread_rwlock_init(invalidate_rwlock, &rwlattr);
	pthread_rwlock_init(g_fcache_rwlock, &rwlattr);

	//printf("dlookup_rwlock %lu, dcache_rwlock %lu\n", dlookup_rwlock,dcache_rwlock);
	pthread_rwlock_init(shm_slab_rwlock, &rwlattr);
	pthread_rwlock_init(shm_lru_rwlock, &rwlattr);

	pthread_mutex_init(bitmap_lock, NULL);
	pthread_mutex_init(nblock_lock, NULL);
	pthread_mutex_init(sblock_lock, NULL);
	pthread_mutex_init(applog_lock, NULL);
	pthread_mutex_init(fileno_lock, NULL);
	pthread_mutex_init(allossd_lock,NULL);
	pthread_mutex_init(allossd1_lock,NULL);
	pthread_mutex_init(allossd2_lock,NULL);
	pthread_mutex_init(allossd3_lock,NULL);
	pthread_mutex_init(allonvm_lock,NULL);
}


//256bit



#define ATTR_MODE	(1 << 0)
#define ATTR_UID	(1 << 1)
#define ATTR_GID	(1 << 2)
#define ATTR_SIZE	(1 << 3)
#define ATTR_ATIME	(1 << 4)
#define ATTR_MTIME	(1 << 5)
#define ATTR_CTIME	(1 << 6)
#define ATTR_ATIME_SET	(1 << 7)
#define ATTR_MTIME_SET	(1 << 8)
#define ATTR_FORCE	(1 << 9) /* Not a change, but a change it */
#define ATTR_ATTR_FLAG	(1 << 10)
#define ATTR_KILL_SUID	(1 << 11)
#define ATTR_KILL_SGID	(1 << 12)
#define ATTR_FILE	(1 << 13)
#define ATTR_KILL_PRIV	(1 << 14)
#define ATTR_OPEN	(1 << 15) /* Truncating from open(O_TRUNC) */
#define ATTR_TIMES_SET	(1 << 16)
#define ATTR_TOUCH	(1 << 17)




static void append_direntry_log(struct fuckfs_dentry *entry){
	memcpy(logaddr, entry, sizeof(struct fuckfs_dentry));
	msync(logaddr,sizeof(struct fuckfs_dentry), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct fuckfs_dentry);
	//size =2sizeof(fuckfs_extent)
}

static void append_dir_inode_entry(struct inode *dir, struct fuckfs_dentry *entry){
	__le16 links_count;
	entry->entry_type = DIR_LOG;
	//entry->ino = cpu_to_le64(dentry->ino);
	//entry->name_len = dentry->d_name.len;
	//memcpy(entry->name, dentry->d_name.name, dentry->d_name.len);
	//entry->name[dentry->d_name.len] = '\0';
	entry->file_type = 0;
	entry->invalid = 0;
	//entry->mtime = cpu_to_le32(dir->i_mtime.tv_sec);
	//entry->size = cpu_to_le64(dir->i_size);

	//links_count = cpu_to_le16(dir->i_nlink);
	//if (links_count == 0 && link_change == -1)
	//	links_count = 0;
	//else
	//	links_count += link_change;
	//entry->links_count = cpu_to_le16(links_count);

	/* Update actual de_len */
	//entry->de_len = cpu_to_le16(de_len);
	logaddr = logstart+logoffset;
	memcpy(logaddr, entry, sizeof(struct fuckfs_dentry));
	msync(logaddr,sizeof(struct fuckfs_dentry), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct fuckfs_dentry);
}
static void append_file_write_entry(struct inode *inode, struct fuckfs_extent *entry){

	entry->entry_type = FILE_WRITE;
	//entry->mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	logaddr = logstart+logoffset;
	memcpy(logaddr, entry, sizeof(struct fuckfs_extent));
	msync(logaddr,sizeof(struct fuckfs_extent), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct fuckfs_extent);
}

static void append_link_change_entry(struct inode *inode, struct link_change_entry *entry){

	entry->entry_type = LINK_CHANGE;
	//entry->links = cpu_to_le16(inode->i_nlink);
	//entry->ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	//entry->flags = cpu_to_le32(inode->i_flags);
	//entry->generation = cpu_to_le32(inode->i_generation);
	logaddr = logstart+logoffset;
	memcpy(logaddr, entry, sizeof(struct link_change_entry));
	msync(logaddr,sizeof(struct link_change_entry), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct link_change_entry);
}

static void append_setattr_entry(struct inode *inode, struct setattr_logentry *entry)//, struct iattr *attr)
{
	unsigned int ia_valid = 1, attr_mask;

	/* These files are in the lowest byte */
	attr_mask = ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_SIZE |
			ATTR_ATIME | ATTR_MTIME | ATTR_CTIME;

	entry->entry_type	= SET_ATTR;
	entry->attr	= ia_valid & attr_mask;
	//entry->mode	= cpu_to_le16(inode->i_mode);
	//entry->uid	= cpu_to_le32(i_uid_read(inode));
	//entry->gid	= cpu_to_le32(i_gid_read(inode));
	//entry->atime	= cpu_to_le32(inode->i_atime.tv_sec);
	//entry->ctime	= cpu_to_le32(inode->i_ctime.tv_sec);
	//entry->mtime	= cpu_to_le32(inode->i_mtime.tv_sec);

	//if (ia_valid & ATTR_SIZE)
	//	entry->size = cpu_to_le64(attr->ia_size);
	//else
	//entry->size = cpu_to_le64(inode->i_size);
	logaddr = logstart+logoffset;
	memcpy(logaddr, entry, sizeof(struct setattr_logentry));
	msync(logaddr,sizeof(struct setattr_logentry), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct setattr_logentry);
}



void loginit(char logname[]){
	//printf("log init\n");
	int fd=open(logname,O_RDWR);
	if (fd < 0)
   	{
        printf("cannot open %s.\n",logname);
        return -1;
   	}
   	logstart = (char *)mmap(NULL, logspace, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   	logbase = logstart;
   	logoffset=0;
   	logend = logstart+logspace;
   	//if(errno!=0)
	//	userfs_info("%s\n", strerror(errno));
   	//printf("logstart %lu\n", logstart);
   	//printf("logaddr %lu\n", logaddr);
   	//printf("logend %lu\n", logend);
}
/*
void *base;
static void *get_block(u64 block)
{
	//printf("get block is %d\n",block);
	char cnode[4096];
	u64 *rbnode;
	//如何将一个块中存储的uint64的数组读取出来
	//block = block <<page_shift;
	//printf("mmapstart is %lu\n",mmapstart);
	memcpy(cnode, mmapstart+block,4096);
	//printf("memcpy\n");
	rbnode =(u64 *)cnode;
	//printf("cnode %s\n",cnode);
	//printf("rbnode =(u64 *)cnode %d\n",rbnode[0]);
	//printf("rbnode =(u64 *)cnode %lu\n",rbnode);
	return rbnode;
}
/*
static int recursive_find_region(u64 block,	u32 height, unsigned long first_blocknr, unsigned long last_blocknr,
	int *data_found, int *hole_found, int hole)
{
	printf("recursive_find_region1 %d\n");
	unsigned int meta_bits = META_BLK_SHIFT;
	u64 *node;
	unsigned long first_blk, last_blk, node_bits, blocks = 0;
	unsigned int first_index, last_index, i;

	node_bits = (height - 1) * meta_bits;
	printf("recursive_find_region1\n");
	first_index = first_blocknr >> node_bits;
	last_index = last_blocknr >> node_bits;
	printf("recursive_find_region1\n");
	char cnode[4096];
	memcpy(cnode, mmapstart+block,4096);
	node = (u64 *)cnode; //将数据

	printf("node is %d\n",node[0]);
	for (i = first_index; i <= last_index; i++) {
		printf("recursive_find_region2\n");
		if (height == 1 || node[i] == 0) {

			if (node[i]) {
				*data_found = 1;
				if (!hole)
					goto done;
			} else {
				*hole_found = 1;
			}

			if (!*hole_found || !hole)
				blocks += (1UL << node_bits);
		} else {
			printf("else\n");
			first_blk = (i == first_index) ?  (first_blocknr &
				((1 << node_bits) - 1)) : 0;

			last_blk = (i == last_index) ? (last_blocknr &
				((1 << node_bits) - 1)) : (1 << node_bits) - 1;

			blocks += recursive_find_region(node[i], height - 1,
				first_blk, last_blk, data_found, hole_found,
				hole);     //递归查找
			if (!hole && *data_found)
				goto done;
			// cond_resched();
		}
	}
done:
	return blocks;
}
 */

/*
//查找offset在文件的第几块？
unsigned long fuckfs_find_region(struct inode *inode, loff_t *offset, int hole)
{
	printf("fuckfs_find_region\n");
	//struct super_block *sb = inode->i_sb;
	//struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	unsigned int data_bits = 12;
	unsigned long first_blocknr, last_blocknr;
	unsigned long blocks = 0, offset_in_block;
	int data_found = 0, hole_found = 0;
	printf("fuckfs_find_region offset %d\n",*offset);
	printf("fuckfs_find_region inode->size %d\n",inode->size);
	if (*offset >= inode->size)
		return -ENXIO;

	if (!inode->blocks || !inode->root) {
		if (hole)
			return inode->size;
		else
			return -ENXIO;
	}
	//printf("fuckfs_find_region\n");
	offset_in_block = *offset & ((1UL << data_bits) - 1);

	if (inode->height == 0) {
		data_found = 1;
		goto out;
	}

	first_blocknr = *offset >> data_bits;
	last_blocknr = inode->size >> data_bits;

	printf("find_region offset %llx, first_blocknr %lx,"
		" last_blocknr %lx hole %d\n",
		  *offset, first_blocknr, last_blocknr, hole);
	printf("inode->root is %d\n", inode->root);
	blocks = recursive_find_region(inode->root, inode->height,
		first_blocknr, last_blocknr, &data_found, &hole_found, hole);
	
out:
	// Searching data but only hole found till the end 
	//printf("fuckfs_find_region out\n");
	if (!hole && !data_found && hole_found)
		return -ENXIO;
	//printf("fuckfs_find_region out\n");
	if (data_found && !hole_found) {
		// Searching data but we are already into them 
		if (hole)
			// Searching hole but only data found, go to the end 
			*offset = inode->size;
		return 0;
	}
	//printf("fuckfs_find_region out\n");
	// Searching for hole, hole found and starting inside an hole 
	if (hole && hole_found && !blocks) {
		// we found data after it 
		if (!data_found)
			// last hole 
			*offset = inode->size;
		return 0;
	}
	//printf("fuckfs_find_region3\n");
	if (offset_in_block) {
		blocks--;
		*offset += (blocks << data_bits) +
			   ((1 << data_bits) - offset_in_block);
	} else {
		*offset += blocks << data_bits;
	}

	return 0;
}

*/

void sb_init(struct super_block *sb){
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_rwlock_init(&sb->wcache_rwlock, &rwlattr);
	//sb->wcache = NULL;

	//INIT_LIST_HEAD(&sb->wcache_head);
	//sb->wcache_list= skiplist_new();
	//sb->wcache_head = (struct list_head *)malloc(sizeof(struct list_head));
	//INIT_LIST_HEAD(sb->wcache_head);
	//sb->n_wcache_entries = 0;
	struct timeval curtime;
	gettimeofday(&curtime,NULL);
	sb->starttime = curtime.tv_sec *100000 + curtime.tv_usec;
	//sb->wcache_hash = kh_init(wcache);

}



int nvm =0;
int ssd =1;
void *kmem;



void init_fs(void)
{
	//if(errno!=0)
	//	userfs_info("%s\n", strerror(errno));
	struct timeval start,end;
	gettimeofday(&start, NULL);
    printf("init_fs start sec %d, usec %d\n", start.tv_sec, start.tv_usec);
	printf("init_fs\n");
	system("echo 3 > /proc/sys/vm/drop_caches");
	int ret=0;
	int i;
	char buf[10]="fuckfs";
	const char *perf_profile;
	//printf("test syscall 325\n");
	//int inode_size;
	//unsigned long *fsinode_bitmap;
	//syscall(325, inode_size,fsinode_bitmap);
	//writethdnum = nvmnum*7+ssdnum;
	//writethdnum = nvmnum;
	writethdnum = nvmnum*1+ssdnum;
	//writethdnum = nvmnum*7;
	readthdnum = nvmnum*4+ssdnum;
	committhdnum = nvmnum+ssdnum;

	

#ifdef CONCURRENT
//a pool for each device
#ifdef EACHPOOL
	for(i=1;i<=(nvmnum+ssdnum);i++){
		writepool[i] = thpool_init(2);
		//threadpool_create(4,QUEUE_SIZE);
		if(writepool[i]==NULL){
			panic("allocate pool error\n");
		}
		readpool[i] = threadpool_create(8,QUEUE_SIZE);
	}
#endif
	//printf("init threadpool\n");
	//writethdpool = threadpool_create(writethdnum,QUEUE_SIZE);
	//readthdpool = threadpool_create(readthdnum,QUEUE_SIZE);
	//committhdpool = threadpool_create(committhdnum,4);
#endif
	//if(errno!=0)
	//	userfs_info("%s\n", strerror(errno));
	loginit(logname);
	//cache_init();  //maybe不要管
	device_init();

	mmapstart = g_bdev[1]->map_base_addr;

	entrynum=0;
	printf("initialized %d\n", initialized);
#if 1
	//syscall(350,13130000);
	if (!initialized) {
	//==============parameter set==============
	
		
	
		//inode bitmap
		//申请inode
		//申请bitmap for NVM space 
		//申请bitmap for SSD space
		
		//printf("open /dev/pmem2\n");
		/*
		int pmemfd=open("/dev/pmem1",O_RDWR);
		if (pmemfd < 0)
   		{
          printf("cannot open /dev/mem.");
          return -1;
   		}
		*/
		//struct dfree_list *nvm_free_list;
		//struct dfree_list *ssd_free_list;
		
		//printf("init_structure\n");
		//fsinode_bitmap =(unsigned long long *) malloc (sizeof(unsigned long long)*inode_num);
		//fsinode_map= (struct s_inode_bitmap *) malloc (sizeof(struct s_inode_bitmap));
		//nvm_free_list= (struct dfree_list *) malloc (sizeof(struct dfree_list));
		//ssd_free_list= (struct dfree_list *) malloc (sizeof(struct dfree_list));
		//printf("syscall\n");
		/*
		ret = syscall(325,2, base, fsinode_table, 0);
		if(ret<0){
			panic("syscall request inode table error\n");
		}
		inodenum = ret;
		for(i=0;i<ret;i++){
				printf("inode number is %ld\n",fsinode_table[i]);
		}
		*/

   		


		//asmlinkage static unsigned long long sys_mycall(char* filename, unsigned long long *pos, int *len, int *que, int* i, int mode) //定义自己的系统调用,把用户空间指针传进来
		//NVM Free list
		//printf("end syscall(325)\n");

		//printf("start syscall(326)\n");
		//ret = syscall(326, NVMSIZE, nrangenum, *nblock_table, nvm, 0);
		//if(ret<0){
		//	panic("syscall request nvm space error\n");
		//}
		/*
		else{
			for(i=0;i<nrangenum;i++){
				printf("nvm range block_low %d\n", nblock_table[i][0]);
				printf("nvm range block_high %d\n", nblock_table[i][1]);
			}
			
		}
		*/
		//printf("start syscall(326) %d\n",SSDSIZE);
		//ret = syscall(326, SSDSIZE,nrangenum, *sblock_table, ssd, 0);
		//if(ret<0){
		//	panic("syscall request ssd space error\n");
		//}
		/*
		else{
			for(i=0;i<srangenum;i++){
				printf("ssd range block_low %d\n", sblock_table[i][0]);
				printf("ssd range block_high %d\n", sblock_table[i][1]);
			}
			
		}
		
		ssd1writespace=0;
		ssd2writespace=0;
		ssd3writespace=0;
		*/
		/*INIT_LIST_HEAD(&filenos.list);
		for(i=0;i<FILENUM;i++){
			struct Fileno* tmpfino =(struct Fileno*)malloc(sizeof(struct Fileno));

			tmpfino->fno=i+1;
			list_add_tail(&(tmpfino->list),&(filenos.list));
		}*/
		//printf("sizeof(struct setattr_logentry) is %d\n",sizeof(struct setattr_logentry));
		//printf("sizeof(struct fuckfs_extent) is %d\n",sizeof(struct fuckfs_extent));
		//printf("sizeof(struct fuckfs_dentry) is %d\n",sizeof(struct fuckfs_dentry));
		//printf("sizeof(struct link_change_entry) is %d\n",sizeof(struct link_change_entry));

/*
		unsigned long startaddr = 0;
		unsigned long endaddr = 4294967296UL;
		printf("addr %ld ---- %ld\n",startaddr, endaddr);
		mmapstart = (char *)mmap(NULL, endaddr, PROT_READ | PROT_WRITE, MAP_SHARED, pmemfd, 0);
		if(mmapstart < 0){
			printf("mmap failed.");
			return -1;
		}
		close(pmemfd);   //close file
*/
		//printf("mmapstart %lu\n",mmapstart);
		//memcpy(mmapstart+startaddr, buf, 10);

		//char getbuf[10];
		//msync(mmapstart,endaddr,PROT_READ | PROT_WRITE);
		//munmap(mmapstart, endaddr); //destroy map memory
   		//close(fd);   //close file
   		//验证数据是否写入正确
   		//printf("start syscall(327)\n");
		//syscall(327, startaddr, getbuf,0);
		//printf("getbuf %s\n",getbuf);

		//vfs inode怎么办
		//fetchinode时实际数据地址怎么返回，其中包括NVM中的地址和SSD中的地址。
		//同样，updateinode时实际数据地址怎么传送，也包括两种地址。 红黑树？
		//printf("start syscall(328)\n");
/*		char filename[256]="/mnt/pmem_emul/1.txt";
		//unsigned long inode_no;
		struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		ret=syscall(328, filename, tinode, 0, 1, 0 );
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		//printf("ret %d\n", ret);
		//printf("inode ino 1 is %d\n", inode_no);
		//printf("tinode ino is %d\n", tinode->i_ino);
		//printf("tinode root is %d\n",tinode->root);
		//printf("tinode height is %d\n",tinode->height);
		//printf("tinode i_blk_type is %d\n",tinode->i_blk_type);
		//memcpy(getbuf,mmapstart+ tinode->root,10);
		//printf("getbuf %s\n",getbuf);
		//memcpy(getbuf,mmapstart+ tinode->root+1,10);
		//printf("getbuf %s\n",getbuf);
		struct inode *inode = inodecopy(tinode);
		//给定inode号，如何获取inode数据？？？
		//loff_t offset=1;
		//fuckfs_find_region(inode, &offset,0);
		//printf("end fuckfs_find_region %d\n", offset);
		//offset =409;
		//fuckfs_find_region(inode, &offset,0);
		//printf("end fuckfs_find_region %d\n", offset);
		//offset =1235;
		//fuckfs_find_region(inode, &offset,0);
		//printf("end fuckfs_find_region %d\n", offset);
		//memcpy(getbuf,mmapstart+ offset,10);
		//printf("getbuf %s\n",getbuf);

		//printf("start syscall(328) 2\n");
		//tinode->i_size = 128;
		ret=syscall(328, filename, tinode, 0, 1, 1);
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		//printf("end syscall(328)\n");
*/

		
		//loginit(logname);

	
		struct inode *root_inode;
		//disk_sb = (struct disk_superblock *)userfs_zalloc(sizeof(struct disk_superblock) * (g_n_devices + 1));

		sb = (struct super_block *)userfs_zalloc(sizeof(struct super_block));
		sb_init(sb);
		
		lru_options_t options;
		options.num_slot=4;
		options.node_size=6;
		inodelru = malloc_lru(&options);
		//INIT_LIST_HEAD(&(inodelru->list));
		//struct list_head *head = &(inodelru->list);
		
		//device_init();

		//mmapstart = g_bdev[1]->map_base_addr;
	/*
		char buf[1024];
		u64 bp = 12849152;
		struct buffer_head *bh;
		bh=bh_get_sync_IO(1, bp>>g_block_size_shift, BH_NO_DATA_ALLOC);
		bh->b_data= buf;
		bh->b_size = g_block_size_bytes;
		//printf("spacesize is %d\n", spacesize);
		int ret = userfs_write(bh);
		printf("userfs_write %d\n",ret);
	*/
		debug_init();  //不要管

		cache_init();  //maybe不要管
		//printf("cache init\n");
		shared_memory_init();
		//printf("shared_memory_init\n");
		locks_init();

		//read_superblock(g_root_dev);  //主要实现记录syscall中的数据结构
		printf("syscall 335\n");
		userfs_file_init();
		ADDRINFO = (struct fuckfs_addr_info *)malloc(sizeof(struct fuckfs_addr_info));
		uint64_t inode_base=syscall(335, ADDRINFO,30000);
#if 0
		ADDRINFO->ssd1_block_addr = ADDRINFO->ssd1_block_addr <<9;
		ADDRINFO->ssd2_block_addr = ADDRINFO->ssd2_block_addr <<9;
		ADDRINFO->ssd3_block_addr = ADDRINFO->ssd3_block_addr <<9;
#endif
		nvmstartblock=ADDRINFO->nvm_block_addr;
		printf("nvm_block_addr %d\t", ADDRINFO->nvm_block_addr);
		printf("ssd1writespace %d, ssd2writespace %d, ssd3writespace %d\n", ADDRINFO->ssd1_block_addr,ADDRINFO->ssd2_block_addr,ADDRINFO->ssd2_block_addr);
		printf("inodebase %d\n", inode_base);
		for(i=0;i<inonum;i++){
   			fsinode_table[i]=inode_base+ 128*i;
   		}
		//printf("userfs_file_init()\n");

		//how to get root inode
		char rootname[256]="/mnt/pmem_emul";
		
		//struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		//ret=syscall(328, rootname, tinode, 0, 1, 0 );
		//使用tinode初始化rootinode, 
		//printf("iget root inode\n");
		root_inode = iget(ROOTINO);
	 	//= inodecopy(tinode);
		if (!dlookup_find(rootname))
			dlookup_alloc_add(root_inode, rootname);
		//inode->i_sb = sb;
/*		root_inode->fcache = NULL;
		root_inode->n_fcache_entries = 0;
		pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

		pthread_rwlock_init(&root_inode->fcache_rwlock, &rwlattr);
		//ip->fcache = NULL;
		//ip->n_fcache_entries = 0;

#ifdef KLIB_HASH
		root_inode->fcache_hash = kh_init(fcache);
#endif
*/
		//ret = syscall(330, rootname, &root_inode);
		//read_root_inode(g_root_dev);  //得到的inode怎么处理，放入hash cache中？
		
		//printf("read_root_inode(g_root_dev)\n");
		//printf("LibFS is initialized\n");

		initialized = 1;

		perf_profile = getenv("USERFS_PROFILE");

		if (perf_profile) 
			enable_perf_stats = 1;		
		else
			enable_perf_stats = 0;

		memset(&g_perf_stats, 0, sizeof(libfs_stat_t));

		clock_speed_mhz = get_cpu_clock_speed();

		load_files(root_inode);

		//syscall(331,128);
		//syscall(331,128768);
		//fetch_ondisk_dentry(root_inode);

	}
#endif

#if 0
#ifdef USE_SLAB
	unsigned long memsize_gb = 4;
#endif

	if (!initialized) {
		//const char *perf_profile;
		const char *device_id;
		uint8_t dev_id;
		int i;

		device_id = getenv("DEV_ID");

		// TODO: range check.
		if (device_id)
			dev_id = atoi(device_id);
		else
			dev_id = 4;


#ifdef USE_SLAB
		printf("USE_SLAB\n", );
		userfs_slab_init(memsize_gb << 30); 
#endif
		g_ssd_dev = 2;
		g_hdd_dev = 3;
		g_log_dev = dev_id;

		// This is allocated from slab, which is shared
		// between parent and child processes.
		disk_sb = (struct disk_superblock *)userfs_zalloc(
				sizeof(struct disk_superblock) * (g_n_devices + 1));

		for (i = 0; i < g_n_devices + 1; i++) 
			sb[i] = (struct super_block *)userfs_zalloc(sizeof(struct super_block));

		device_init();

		debug_init();  //不要管

		cache_init();  //maybe不要管
		printf("cache init\n");
		shared_memory_init();
		printf("shared_memory_init\n");
		locks_init();
		printf("start read_superblock\n");
		read_superblock(g_root_dev); //read superblock 为什么一直读ondisk inode

#ifdef USE_SSD
		printf("USE_SSD\n");
		read_superblock(g_ssd_dev);
#endif
#ifdef USE_HDD
		printf("USE_HDD\n");
		read_superblock(g_hdd_dev);
#endif
		read_superblock(g_log_dev);   //read superblock

		userfs_file_init();
		printf("userfs_file_init()\n");
		init_log(g_log_dev);

		printf("init_log(g_log_dev)\n");
		// read root inode in NVM 
		read_root_inode(g_root_dev);
		
		printf("read_root_inode(g_root_dev)\n");
		userfs_info("LibFS is initialized with id %d\n", g_log_dev);

		initialized = 1;

		perf_profile = getenv("USERFS_PROFILE");

		if (perf_profile) 
			enable_perf_stats = 1;		
		else
			enable_perf_stats = 0;

		memset(&g_perf_stats, 0, sizeof(libfs_stat_t));

		clock_speed_mhz = get_cpu_clock_speed();
	}
#endif
	//printf("end init_fs\n");
	gettimeofday(&end, NULL);
    printf("init_fs end sec %d, usec %d\n", end.tv_sec, end.tv_usec);
}

void load_files(struct inode * ip){
	//遍历目录下所有文件
	printf("load_files inum %d\n",ip->inum);
	char *top;
	int rlen;
	//struct inode *inode;
	//__le64 blk_base = syscall(339,inode->inum);
	//syscall(338, inode->inum, "file", 4);
	__le64 blk_base = ip->root;
	char *block = (char *)malloc(g_block_size_bytes);
	userfs_info("blk_base %lu\n",blk_base);

	memcpy(block, mmapstart+blk_base, g_block_size_bytes);
	printf("memcpy\n");
	struct fs_direntry *de = (struct fs_direntry *)block;
	top = block + g_block_size_bytes;
	while ((char *)de < top) {
        printf("fetch dentry is %d\t %d\t %s\t %d\t\n", de->ino, de->de_len,de->name,de->name_len);
        if(de->name[0]!='.'){
        	
        	struct inode * inode = iget(de->ino);
        	if(inode->itype==T_FILE){
        		load_blktree(inode);
        	}
        	

        	struct ufs_direntry * uden= (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
        	struct fs_direntry * direntry= (struct fs_direntry *)malloc(sizeof(struct fs_direntry));
        	direntry->ino = de->ino;
			direntry->file_type=inode->itype;
			strncpy(direntry->name,de->name,de->name_len);
			uden->addordel=Ondisk;
			uden->direntry=direntry;
        	pthread_rwlock_wrlock(&ip->art_rwlock);
			art_insert(&ip->dentry_tree, de->name, de->name_len, uden);
			pthread_rwlock_unlock(&ip->art_rwlock);
			de_cache_alloc_add(ip, direntry->name, inode);
			userfs_info("inode %lu, inode->blktree %lu,uden %lu\n",inode, inode->blktree,uden);

        }
        rlen = le16_to_cpu(de->de_len);
        if (rlen <= 0)
            return -1;
        de = (struct pmfs_direntry *)((char *)de + rlen);
    }

}


struct blknode{
	uint8_t height;
	unsigned long offset;
	unsigned long addr;
	struct list_head list;
};


void load_blktree(struct inode *inode)
{
	userfs_info("load_blktree inode %d\n",inode->inum);
	int ret = 0;
	uint8_t devid=1;
	//handle_t handle;
	//int data_found = 0, hole_found = 0;
	unsigned int data_bits=12;
	offset_t offset = 0;

	__le64 node[512];
	u64 bp = 0;
	unsigned long last_off;
	uint64_t flag =3;
	flag= ~(flag<<62);
	//printf("flag %lu\n", flag);
	//printf("mmapstart %lu\n", mmapstart);
	//unsigned long blocknr, first_blocknr, last_blocknr, blocks;
	//if (offset >= inode->ksize)
	//	return -ENXIO;
	last_off = inode->size >> data_bits;

	//userfs_info("inode->height %d, inode->root %lu, inode->size %d\n", inode->height,inode->root,inode->size);
	if (inode->height == 0 && inode->root==0) {
		struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		userfs_info("fetch inode ino %d\n",inode->inum);
		ret=syscall(336, inode->inum, tinode);
		//ret=syscall(328, filename, tinode, 0, 1, 0 );
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		inode->root=tinode->root;
		inode->height=tinode->height;
	}
	//userfs_info("inode->height %d, inode->root %lu, inode->size %d\n", inode->height,inode->root,inode->ksize);
	//userfs_info("inode->blktree %lu\n",inode->blktree);
	if(inode->height == 0){
		devid = inode->root>>62;
		//printf("inode devid %d\n", devid);
		if (devid>0){
			//bmap_req->block_no =(inode->root & flag)>>g_block_size_shift;
			//bmap_req->dev =devid+1;
			//bmap_req->blk_count_found = 512;
			//offset值是多少
			//bmap_req->start_offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);
			//userfs_info("bmap_req->start_offset %d, blocknode->fileoff %d\n",bmap_req->start_offset,blocknode->fileoff);
			struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
			blocknode->fileoff = offset;
			blocknode->devid = devid+1;
			blocknode->size =inode->size;
			blocknode->addr = (inode->root & flag )>>g_block_size_shift;

			pthread_rwlock_wrlock(&inode->blktree_rwlock);
			block_insert(&(inode->blktree),blocknode);
			pthread_rwlock_unlock(&inode->blktree_rwlock);
			//printf("inode->height==0. devid>0\n");
			return 0;
		}else{
			memcpy(node, mmapstart+inode->root, g_block_size_bytes);
			//把对应的512个地址全加入blocknode中
			//offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);
			//uint64_t key = (bmap_req->start_offset >> g_block_size_shift)%lblock_addr;
			//userfs_info("key %d\n",key);
			
			for(int i=0;i<last_off;i++){
				devid = node[i]>>62;
				struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
				if(devid>0){
					blocknode->devid = devid+1;
					blocknode->addr = (node[i] & flag)>>g_block_size_shift;
				}else{
					blocknode->devid = devid+1;
					blocknode->addr = node[i] >>g_block_size_shift;
				}
				blocknode->fileoff = offset;
				blocknode->size =g_block_size_bytes;
				offset +=g_block_size_bytes;
				//userfs_info("blocknode addr %ld, fileoff %d, size %d\n", blocknode->addr, blocknode->fileoff, blocknode->size);
				pthread_rwlock_wrlock(&inode->blktree_rwlock);
				block_insert(&(inode->blktree),blocknode);
				pthread_rwlock_unlock(&inode->blktree_rwlock);

			}
			
			return 0;
		}
	
	}

	struct list_head queue;
	INIT_LIST_HEAD(&queue);
	last_off = inode->size >> data_bits;
	bp= le64_to_cpu(inode->root);
	u32 height, bit_shift;
	height = inode->height;
	struct blknode *blknode = (struct blknode *)malloc(sizeof(struct blknode));
	struct blknode *_blknode;
	blknode->height = height;
	blknode->offset =0;
	blknode->addr=bp;
	//userfs_info("inode->root %lu, blknode->height %d, blknode->addr %lu\n",inode->root,blknode->height,blknode->addr);
	list_add_tail(&blknode->list,&queue);
	blknode=list_first_entry(&queue,struct blknode,list);
	unsigned int last_idx;
	while (blknode->height > 0) {
		memcpy(node, mmapstart+blknode->addr, g_block_size_bytes);
		bit_shift = height * META_BLK_SHIFT;
		last_idx = last_off >> bit_shift;
		//userfs_info("bit_shift %d,last_idx %d\n", bit_shift,last_idx);
		for(int i=0;i<last_idx;i++){
			struct blknode *blk_node = (struct blknode *)malloc(sizeof(struct blknode));
			blk_node->height = blknode->height-1;
			blk_node->offset = i<<bit_shift;
			blk_node->addr = node[i];
			//userfs_info("idex %d, height %d, addr %lu\n",i,blk_node->height,node[i]);
			list_add_tail(&blk_node->list,&queue);

		}
		list_del(&(blknode->list));
		blknode=list_first_entry(&queue,struct blknode,list);
	}

	//userfs_info("height %d\n",height);
	list_for_each_entry_safe(blknode, _blknode, &queue, list){
		devid = blknode->addr >>62;
		//userfs_info("blknode->addr %lu, devid %d\n",blknode->addr,devid);
		if (devid >0){
			
			struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
			blocknode->devid = devid+1;
			blocknode->size =g_lblock_size_bytes;
			blocknode->addr = (blknode->addr & flag)>>g_block_size_shift;
			blocknode->fileoff = offset;
			offset+=g_lblock_size_bytes;
			//userfs_info("blocknode->fileoff %d, addr %lu\n", blocknode->fileoff,blocknode->addr);
			pthread_rwlock_wrlock(&inode->blktree_rwlock);
			block_insert(&(inode->blktree),blocknode);
			pthread_rwlock_unlock(&inode->blktree_rwlock);
			//return 0;
		}else{
			//printf("devid == 0\n");
			memcpy(node, mmapstart+blknode->addr, g_block_size_bytes);
			//uint64_t key = (bmap_req->start_offset >> g_block_size_shift)%lblock_addr;
			//offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);

			for(int i=0;i<lblock_addr;i++){
				struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
				//userfs_info("offset %d\n", offset);
				devid = node[i]>>62;
				if(devid>0){
					blocknode->devid = devid+1;
					blocknode->addr = (node[i] & flag)>>g_block_size_shift;
				}else{
					blocknode->devid = devid+1;
					blocknode->addr = node[i] >>g_block_size_shift;
				}
				blocknode->fileoff = offset;
				blocknode->size =g_block_size_bytes;
				offset +=g_block_size_bytes;
				//printf("blocknode->fileoff %d, addr %lu\n", blocknode->fileoff,blocknode->addr);
				pthread_rwlock_wrlock(&inode->blktree_rwlock);
				block_insert(&(inode->blktree),blocknode);
				pthread_rwlock_unlock(&inode->blktree_rwlock);

			}
		//return 0;
		}
	}
	//userfs_info("inode->blktree %lu\n",inode->blktree);
	/*
	struct block_node *bnode;
	for(uint64_t off=0;off<inode->size;off+=2097152){
		bnode= block_search(&(inode->blktree),off);
		printf("bnode fileoff %d, addr %lu\n", bnode->fileoff, bnode->addr);
	}
	*/
	return;

}

///////////////////////////////////////////////////////////////////////
// Physical block management

void read_superblock(uint8_t dev)
{

	//printf("read_superblock %d\n",dev);
	uint32_t inum;
	int ret;
	struct buffer_head *bh;
	//struct dinode dip;
	// 1 is superblock address
	//printf("read_superblock0\n");
	bh = bh_get_sync_IO(dev, 1, BH_NO_DATA_ALLOC);
	//printf("read_superblock1\n");
	bh->b_size = g_block_size_bytes;
	bh->b_data = userfs_zalloc(g_block_size_bytes);
	//printf("read_superblock2\n");
	bh_submit_read_sync_IO(bh);
	userfs_io_wait(dev, 1);
	//printf("read_superblock3\n");
	if (!bh)
		panic("cannot read superblock\n");

	//memmove(&disk_sb[dev], bh->b_data, sizeof(struct disk_superblock));
/*
	printf("[dev %d] superblock: size %u nblocks %u ninodes %u "
			"inodestart %lx bmap start %lx datablock_start %lx\n",
			dev,
			disk_sb[dev].size, 
			disk_sb[dev].ndatablocks, 
			disk_sb[dev].ninodes,
			disk_sb[dev].inode_start, 
			disk_sb[dev].bmap_start, 
			disk_sb[dev].datablock_start);
*/	

	//sb[dev]->ondisk = &disk_sb[dev];

/*	sb[dev]->s_inode_bitmap = (unsigned long *)
		userfs_zalloc(BITS_TO_LONGS(disk_sb[dev].ninodes));

	if (dev == g_root_dev) {
		// setup inode allocation bitmap.
		printf("read_superblock %d\n",disk_sb[dev].ninodes);
		for (inum = 1; inum < disk_sb[dev].ninodes; inum++) {
			read_ondisk_inode(dev, inum, &dip);

			if (dip.itype != 0)
				bitmap_set(sb[dev]->s_inode_bitmap, inum, 1);
		}
	}
*/
	userfs_free(bh->b_data);
	bh_release(bh);
}



struct inode *read_ondisk_inode(uint32_t inum){
	userfs_info("read_ondisk_inode fetch inode ino %d\n",inum);
	int ret=0;
	struct inode *inode;
	inode=(struct  inode *)malloc(sizeof(struct inode));
	struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
	ret=syscall(336,inum, tinode);
	if(ret<0){
		panic("syscall fetch inode error\n");
	}
	inode->i_mode = tinode->i_mode;
    //inode->i_opflags = tinode->i_opflags;
    //inode->i_uid = tinode->i_uid;
    //inode->i_gid = tinode->i_gid;
    //inode->flags = tinode->i_flags;
    inode->flags=0;
    inode->flags |= I_VALID;
    inode->inum = inum;
    inode->nlink = tinode->i_nlink;
    inode->size = tinode->i_size;
    inode->ksize = tinode->i_size;
    //时间类型不同
    inode->atime = tinode->i_atime;
    inode->mtime = tinode->i_mtime;
    inode->ctime = tinode->i_ctime;
    /*
    inode->i_atime.tv_nsec = tinode->i_atime.tv_nsec;
    inode->i_mtime.tv_sec = tinode->i_mtime.tv_sec;
    inode->i_mtime.tv_nsec = tinode->i_mtime.tv_nsec/1000;
    inode->i_ctime.tv_sec = tinode->i_ctime.tv_sec;
    inode->i_ctime.tv_nsec = tinode->i_ctime.tv_nsec/1000;
    */
    //userfs_info("inode->flags %d\n",inode->flags);
    //inode->itype = uint8_t(tinode->i_bytes);
    //userfs_info("inode->i_mode %d\n", inode->i_mode);
    if(S_ISDIR(inode->i_mode))
    	inode->itype=T_DIR;
    else if(S_ISREG(inode->i_mode))
    	inode->itype=T_FILE;
    else
    	inode->itype=T_DEV;
    //userfs_info("inode->itype %d\n", inode->itype);
    //inode->i_blkbits = tinode->i_blkbits;
    inode->blocks = tinode->i_blocks;
    //inode->i_state = tinode->i_state;
	inode->height = tinode->height;
    inode->root = tinode->root;
    inode->i_blk_type = tinode->i_blk_type;
    inode->nvmblock = tinode->nvmblock;
    uint8_t devid = tinode->blkoff>>62;
    if(devid==0){
    	inode->nvmoff = tinode->blkoff;
    	inode->ssdoff =inode->nvmoff;
    }else{
    	inode->ssdoff = tinode->blkoff &(((uint64_t)0x1<<62)-1);
    	inode->devid =devid;
    	inode->nvmoff =0;
    }
    //userfs_info("inode->nvmoff %ld\n", inode->nvmoff);
    //userfs_info("tinode->blkoff %ld, (((uint64_t)0x1<<63)-1) %lu\n", tinode->blkoff, (((uint64_t)0x1<<62)-1));
    //userfs_info("inode->ssdoff %ld\n", inode->ssdoff);
    inode->usednvms = ALIGN_FLOOR(inode->ssdoff,g_lblock_size_bytes);
    //printf("inode->itype %d\n", inode->itype);

    //userfs_info("inode->size %d\n", inode->size);
    
    pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	inode->de_cache = NULL;
	//INIT_LIST_HEAD(&inode->i_slru_head);
	
    pthread_mutex_init(&inode->i_mutex, NULL);
	//userfs_info("inode->root %lu\n", inode->root);
    //userfs_info("inode->height %d\n", inode->height);
    userfs_info("read_ondisk_inode ino %d\n",inum);
    return inode;
}
/*
void read_root_inode(uint8_t dev_id)
{
	printf("read_root_inode\n");
	struct dinode _dinode;
	struct inode *ip;

	read_ondisk_inode(dev_id, ROOTINO, &_dinode);
	_dinode.dev = dev_id;
	userfs_debug("root inode block %lx size %lu\n",
			IBLOCK(ROOTINO, disk_sb[dev_id]), dip->size);

	userfs_assert(_dinode.itype == T_DIR);

	ip = ialloc(dev_id, ROOTINO, &_dinode);
}

int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip)
{
	int ret;
	struct buffer_head *bh;
	uint8_t _dev = dev;
	addr_t inode_block;

	userfs_assert(dev == g_root_dev);

	printf("read_ondisk_inode %d %d\n",dev,inum);
	inode_block = get_inode_block(dev, inum);
	printf("read_ondisk_inode %lx\n",inode_block);
	bh = bh_get_sync_IO(dev, inode_block, BH_NO_DATA_ALLOC);
	printf("inode_block %lx\n", inode_block);
	if (dev == g_root_dev) {
		bh->b_size = sizeof(struct dinode);
		bh->b_data = (uint8_t *)dip;
		bh->b_offset = sizeof(struct dinode) * (inum % IPB);
		printf("bh_submit_read_sync_IO\n");
		bh_submit_read_sync_IO(bh);
		printf("bh_submit_read_sync_IO1\n");
		userfs_io_wait(dev, 1);
		printf("userfs_io_wait\n");
	} else {
		panic("This code path is deprecated\n");

		bh->b_size = g_block_size_bytes;
		bh->b_data = userfs_zalloc(g_block_size_bytes);

		bh_submit_read_sync_IO(bh);
		userfs_io_wait(dev, 1);

		memmove(dip, (struct dinode*)bh->b_data + (inum % IPB), sizeof(struct dinode));

		userfs_free(bh->b_data);
	}

	return 0;
}
*/

// Allocate "in-memory" inode. 
// on-disk inode is created by icreate
struct inode* ialloc(uint32_t inum)
{
	//printf("start ialloc \n");
	int ret;
	struct inode *ip;
	pthread_rwlockattr_t rwlattr;

	//userfs_assert(dev == g_root_dev);
	//printf("ialloc\n");
	ip = icache_find(inum);
	if (!ip) 
		ip = icache_alloc_add(inum);

	//ip->_dinode = (struct dinode *)ip;
	//printf("ialloc1\n");
	if (ip->flags & I_DELETING) {
		// There is the case where unlink in the update log is not yet digested. 
		// Then, ondisk inode does contain a stale information. 
		// So, skip syncing with ondisk inode.
		//memset(ip->_dinode, 0, sizeof(struct dinode));
		//ip->dev = dev;
		ip->inum = inum;
		userfs_debug("reuse inode - inum %u\n", inum);
	} else {
		//sync_inode_from_dinode(ip, dip);
		userfs_debug("get inode - inum %u\n", inum);
	}
	//printf("ialloc2\n");
	ip->flags = 0;
	ip->flags |= I_VALID;
	ip->i_ref = 1;
	ip->n_de_cache_entry = 0;
	ip->i_dirty_dblock = RB_ROOT;
	ip->i_sb = sb;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);
	//printf("ialloc3\n");
	pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
	ip->fcache = NULL;
	ip->n_fcache_entries = 0;

#ifdef KLIB_HASH
	userfs_debug("allocate hash %u\n", ip->inum);
	ip->fcache_hash = kh_init(fcache);
#endif
	//printf("ialloc4\n");
	ip->de_cache = NULL;
	pthread_spin_init(&ip->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	
	INIT_LIST_HEAD(&ip->i_slru_head);
	
	pthread_mutex_init(&ip->i_mutex, NULL);
	//printf("ialloc5\n");
	//bitmap_set(sb[dev]->s_inode_bitmap, inum, 1);
	//printf("end ialloc\n");
	return ip;
}

// Allocate a new inode with the given type on device dev.
// A free inode has a type of zero.
struct inode* icreate(uint8_t type)
{
	//printf("icreate\n");
	uint32_t inum;
	int ret;
	//struct dinode dip;
	struct inode *ip;
	pthread_rwlockattr_t rwlattr;

	// FIXME: hard coded. used for testing multiple applications.
	/*if (g_log_dev == 4)
		inum = find_next_zero_bit(sb[dev]->s_inode_bitmap, 
				sb[dev]->ondisk->ninodes, 1);
	else
		inum = find_next_zero_bit(sb[dev]->s_inode_bitmap, 
				sb[dev]->ondisk->ninodes, NINODES/2);
	*/
	/*
	read_ondisk_inode(dev, inum, &dip);

	// Clean (in-memory in block cache) ondisk inode.
	// At this point, storage and in-memory state diverges.
	// Libfs does not write dip directly, and kernFS will
	// update the dip on storage when digesting the inode.
	setup_ondisk_inode(&dip, dev, type);
	*/
	ip = ialloc(inum);

	return ip;
}

/* Inode (in-memory) cannot be freed at this point.
 * If the inode is freed, libfs will read on-disk inode from
 * read-only area. This cause a problem since the on-disk deletion 
 * is not applied yet in kernfs (before digest).
 * idealloc() marks the inode as deleted (I_DELETING). The inode is
 * removed from icache when digesting the inode.
 * from icache. libfs will free the in-memory inode after digesting
 * a log of deleting the inode.
 */
int idealloc(struct inode *inode)
{
	//printf("idealloc\n");
	struct inode *_inode;
	lru_node_t *l, *tmp;
	//userfs_info("inode->i_ref %d\n", inode->i_ref);
	userfs_assert(inode->i_ref < 2);
	//
	if (inode->i_ref == 1 && 
			(inode->flags & I_VALID) && 
			inode->nlink == 0) {
		if (inode->flags & I_BUSY)
			panic("Inode must not be busy!");

		/*
		   if (inode->size > 0)
		   itrunc(inode, 0);
		*/

		inode->flags &= ~I_BUSY;
	}
	//printf("inode\n");
	ilock(inode);
	if(inode->itype == T_FILE){
		RB_EMPTY_ROOT(&inode->blktree);
	}else if(inode->itype == T_DIR){
		art_tree_destroy(&inode->dentry_tree);
	}
	//fcache_del_all(inode);
		//fcache_hash
		
	inode->size = 0;
	/* After persisting the inode, libfs moves it to
	 * deleted inode hash table in persist_log_inode() */
	inode->flags |= I_DELETING;
	inode->itype = 0;

	
	/* delete inode data (log) pointers */
	fcache_del_all(inode);

	//printf("pthread_spin_destroy\n");
	pthread_spin_destroy(&inode->de_cache_spinlock);
	pthread_mutex_destroy(&inode->i_mutex);
	pthread_rwlock_destroy(&inode->fcache_rwlock);

	//userfs_info("iunlock %lu\n",inode);
	iunlock(inode);

	userfs_debug("dealloc inum %u\n", inode->inum);
	//userfs_info("dealloc inum %u\n", inode->inum);
#if 0 // TODO: do this in parallel by assigning a background thread.
	list_for_each_entry_safe(l, tmp, &inode->i_slru_head, list) { 
		HASH_DEL(lru_hash_head, l);
		list_del(&l->list);
		userfs_free_shared(l);
	}
#endif

	return 0;
}

// Copy a modified in-memory inode to log.
void iupdate(struct inode *ip)   //更新inode到log中
{
	userfs_assert(!(ip->flags & I_DELETING));

	//if (!(ip->dinode_flags & DI_VALID))
	//	panic("embedded _dinode is invalid\n");

	//if (ip->_dinode != (struct dinode *)ip)
	//	panic("_dinode pointer is incorrect\n");

	userfs_get_time(&ip->mtime);
	ip->atime = ip->mtime; 
	//struct fuckfs_dentry *entry;
	//append_dir_inode_entry(ip, entry);
	//add_to_loghdr(L_TYPE_INODE_UPDATE, ip, 0, 
	//		sizeof(struct dinode), NULL, 0);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
struct inode* iget(uint32_t inum)
{
	//userfs_info("inode iget %d\n",inum);
	struct inode *ip;

	ip = icache_find(inum);
	//userfs_info("iget ip %lu\n", ip);
	if (ip) {
		if ((ip->flags & I_VALID) && (ip->flags & I_DELETING))
			return NULL;

		pthread_mutex_lock(&ip->i_mutex);
		ip->i_ref++;
		pthread_mutex_unlock(&ip->i_mutex);
	} else {
		//struct dinode dip;
		// allocate new in-memory inode
		userfs_debug("allocate new inode by iget %u\n", inum);
		//printf("iget allocate new inode by iget %d\n", inum);
		//read_ondisk_inode(dev, inum, &dip);
		pthread_rwlockattr_t rwlattr;
		ip=read_ondisk_inode(inum); 
		//printf(" end read_ondisk_inode(dev, inum, &dip);\n");
		//printf("ip->inum is %d\n", ip->inum);
		ip = icache_add(ip);
		ip->n_de_cache_entry = 0;
		ip->i_dirty_dblock = RB_ROOT;
		ip->i_sb = sb;
		ip->fcache = NULL;
		ip->n_fcache_entries = 0;
		ip->devid=0;
		//ip->fileno =0;
		//ip->writespace = NULL;
		ip->i_ref=1;
		//pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);
		//printf("ialloc3\n");
		//pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
		//ip->fcache = NULL;
		//ip->n_fcache_entries = 0;
		pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

		pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
		//ip->fcache = NULL;
		//ip->n_fcache_entries = 0;

#ifdef KLIB_HASH
		ip->fcache_hash = kh_init(fcache);
#endif
		ip->blktree=RB_ROOT;
		//userfs_info("ip->blktree %lu\n",ip->blktree);
		pthread_rwlock_init(&ip->blktree_rwlock, &rwlattr);

		ip->de_cache = NULL;
		pthread_spin_init(&ip->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&ip->i_mutex, NULL);
		if(ip->itype==T_DIR)
		{
			pthread_rwlock_init(&ip->art_rwlock, &rwlattr);
			art_tree_init(&ip->dentry_tree);
			ip->inmem_den = 0;
		}
		//ip = ialloc(inum);
	}
	//userfs_info("ip->inum is %d\n", ip->inum);
	userfs_info("ip->itype is %d\n", ip->itype);
	return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode* idup(struct inode *ip)
{
	panic("does not support idup yet\n");

	return ip;
}

void ilock(struct inode *ip)
{
	//userfs_info("start ilock inode %lu, ip->i_mutex %lu, ip->i_mutex %d\n",ip,&ip->i_mutex,ip->i_mutex);
	pthread_mutex_lock(&ip->i_mutex);
	ip->flags |= I_BUSY;
	//userfs_info("end ilock inode %lu, ip->i_mutex %lu, ip->i_mutex %d\n",ip,&ip->i_mutex,ip->i_mutex);
}

void iunlock(struct inode *ip)
{
	//userfs_info("start iunlock inode %lu, ip->i_mutex %lu, ip->i_mutex %d\n",ip,&ip->i_mutex,ip->i_mutex);
	pthread_mutex_unlock(&ip->i_mutex);
	ip->flags &= ~I_BUSY;
	//userfs_info("end iunlock inode %lu, ip->i_mutex %lu, ip->i_mutex %d\n",ip,&ip->i_mutex,ip->i_mutex);
}

/* iput does not deallocate inode. it just drops reference count. 
 * An inode is explicitly deallocated by ideallc() 
 */
void iput(struct inode *ip)
{
	pthread_mutex_lock(&ip->i_mutex);

	userfs_muffled("iput num %u ref %u nlink %u\n", 
			ip->inum, ip->i_ref, ip->nlink);
	//userfs_info("iput num %u inode->i_ref %d nlink %u\n",ip->inum, ip->i_ref, ip->nlink);
	ip->i_ref--;

	pthread_mutex_unlock(&ip->i_mutex);
}

// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
	iunlock(ip);
	iput(ip);
}



unsigned long recursive_find_region( __le64 block, u32 height, unsigned long first_blocknr, unsigned long last_blocknr,
	int *data_found, int *hole_found)
{
	
	/*u64 *node;
	*char cnode[4096];
	*memcpy(cnode, mmapstart+block,4096);
	*node = (u64 *)cnode; //将数据
	*/
	unsigned int meta_bits = META_BLK_SHIFT;
	__le64 node[512];
	unsigned long first_blk, last_blk, node_bits, blocks = 0;
	unsigned int first_index, last_index, i;

	node_bits = (height - 1) * meta_bits;

	first_index = first_blocknr >> node_bits;
	last_index = last_blocknr >> node_bits;

	memcpy(node, mmapstart+block, g_block_size_bytes);

	for (i = first_index; i <= last_index; i++) {
		if (height == 1 || node[i] == 0) {
			if (node[i]) {
				*data_found = 1;
			} else {
				*hole_found = 1;
			}

			if (!*hole_found)
				blocks += (1UL << node_bits);
		} else {
			first_blk = (i == first_index) ?  (first_blocknr &
				((1 << node_bits) - 1)) : 0;

			last_blk = (i == last_index) ? (last_blocknr &
				((1 << node_bits) - 1)) : (1 << node_bits) - 1;

			blocks += recursive_find_region(node[i], height - 1,
				first_blk, last_blk, data_found, hole_found);
			if ( *data_found)
				goto done;
			/* cond_resched(); */
		}
	}
done:
	return blocks;
}

/* Get block addresses from extent trees.
 * return = 0, if all requested offsets are found.
 * return = -EAGAIN, if not all blocks are found.
 * 
 *
* 查找 inode上 rb_root blktree;   //index blk for file，去找块地址
*如果blktree中存在对应块地址，这根据块地址去对应位置读取
*否则，读取对应inode，找到访问btree树，找到对应的地址，读取数据，
*并将地址index到blktree中。
*/
int bmap(struct inode *inode, struct bmap_request *bmap_req)
{
	//printf("bmap inode %d, offset %d\n",inode->inum,bmap_req->start_offset);
	//userfs_info("bmap inode %d, offset %d\n",inode->inum,bmap_req->start_offset);
	int ret = 0;
	uint8_t devid=1;
	//handle_t handle;
	//int data_found = 0, hole_found = 0;
	unsigned int data_bits=12;
	offset_t offset = bmap_req->start_offset;

	__le64 node[512];
	u64 bp = 0;
	unsigned long blocknr;
	uint64_t flag =3;
	flag= ~(flag<<62);
	//printf("flag %lu\n", flag);
	//printf("mmapstart %lu\n", mmapstart);
	//unsigned long blocknr, first_blocknr, last_blocknr, blocks;
	if (offset >= inode->ksize)
		return -ENXIO;


/*
	struct block_node *blocknode =block_search(&(inode->blktree),offset);
	//printf("block_node is %lu\n",blocknode);
	if(blocknode!=NULL){
		bmap_req->block_no=blocknode->addr;
		bmap_req->dev = blocknode->devid;
		bmap_req->blk_count_found = 1;
		//printf("block_node is %d\n",blocknode->addr);
		return 0;
	}
	*/
	
	//offset_in_block = offset & ((1UL << data_bits) - 1);
	
	//userfs_info("offset %d\n",offset);
	//userfs_info("inode->height %d, inode->root %lu, inode->size %d\n", inode->height,inode->root,inode->size);
	if (inode->height == 0 && inode->root==0) {
		struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		userfs_info("fetch inode ino %d\n",inode->inum);
		ret=syscall(336, inode->inum, tinode);
		//ret=syscall(328, filename, tinode, 0, 1, 0 );
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		inode->root=tinode->root;
		inode->height=tinode->height;
	}
	//userfs_info("inode->height %d, inode->root %lu, inode->size %d\n", inode->height,inode->root,inode->ksize);
	if(inode->height == 0){
		devid = inode->root>>62;
		//printf("inode devid %d\n", devid);
		if (devid>0){
			bmap_req->block_no =(inode->root & flag)>>g_block_size_shift;
			bmap_req->dev =devid+1;
			bmap_req->blk_count_found = 512;
			//offset值是多少
			bmap_req->start_offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);
			//userfs_info("bmap_req->start_offset %d, blocknode->fileoff %d\n",bmap_req->start_offset,blocknode->fileoff);
			struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
			blocknode->fileoff = bmap_req->start_offset;
			blocknode->devid = devid+1;
			blocknode->size =g_lblock_size_bytes;
			blocknode->addr = (inode->root & flag )>>g_block_size_shift;

			pthread_rwlock_wrlock(&inode->blktree_rwlock);
			block_insert(&(inode->blktree),blocknode);
			pthread_rwlock_unlock(&inode->blktree_rwlock);
			//printf("inode->height==0. devid>0\n");
			return 0;
		}else{
			memcpy(node, mmapstart+inode->root, g_block_size_bytes);
			//把对应的512个地址全加入blocknode中
			offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);
			uint64_t key = (bmap_req->start_offset >> g_block_size_shift)%lblock_addr;
			//userfs_info("key %d\n",key);
			
			for(int i=0;i<lblock_addr;i++){
				devid = node[key]>>62;
				struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
				if(devid>0){
					blocknode->devid = devid+1;
					blocknode->addr = (node[i] & flag)>>g_block_size_shift;
				}else{
					blocknode->devid = devid+1;
					blocknode->addr = node[i] >>g_block_size_shift;
				}
				if(i==key){
					bmap_req->block_no =blocknode->addr;
					bmap_req->dev =blocknode->devid;
					bmap_req->blk_count_found = 1;
				}
				blocknode->fileoff = offset;
				blocknode->size =g_block_size_bytes;
				offset +=g_block_size_bytes;
				pthread_rwlock_wrlock(&inode->blktree_rwlock);
				block_insert(&(inode->blktree),blocknode);
				pthread_rwlock_unlock(&inode->blktree_rwlock);

			}
			/*
			if(devid>0){
				bmap_req->block_no =(node[key] & flag)>>g_block_size_shift;
				bmap_req->dev =devid +1;
				blocknode->devid = devid+1;
				blocknode->addr = (node[key] & flag)>>g_block_size_shift;
			}else{
				bmap_req->block_no =node[key] >>g_block_size_shift;
				bmap_req->dev = devid+1;
			
				blocknode->devid = devid+1;
				blocknode->addr = node[key] >>g_block_size_shift;
			}
			blocknode->fileoff = offset;
			bmap_req->blk_count_found = 1;
			blocknode->size =g_block_size_bytes;
			pthread_rwlock_wrlock(&inode->blktree_rwlock);
			block_insert(&(inode->blktree),blocknode);
			pthread_rwlock_unlock(&inode->blktree_rwlock);
			*/
			return 0;
		}
		/*	
		bmap_req->block_no =inode->root >>g_block_size_shift;
		bmap_req->dev =1 ;
		bmap_req->blk_count_found = 1;

		blocknode->devid = 1;
		blocknode->size =4096;
		blocknode->addr = inode->root >>g_block_size_shift;
		block_insert(&(inode->blktree),blocknode);
		return 0;
		*/
	}
	//printf("height > 0\n");
	blocknr = offset >> data_bits;
	//last_blocknr = inode->size >> data_bits;

	bp= le64_to_cpu(inode->root);
	u32 height, bit_shift;
	height = inode->height;
	unsigned int idx;

	//userfs_info("height %d\n",height);
	while (height > 0) {
		//printf("bp %lu\n", bp);
		//level_ptr = pmfs_get_block(sb, bp);

		memcpy(node, mmapstart+bp, g_block_size_bytes);
		//userfs_info("memcpy end node %ld\n",node[0]);
	/*	struct buffer_head *bh;
		bh=bh_get_sync_IO(1, bp, BH_NO_DATA_ALLOC);
		bh->b_data= node;
		bh->b_size = g_block_size_bytes;
		//printf("spacesize is %d\n", spacesize);
		ret = userfs_write(bh);
		printf("userfs_write %d\n",ret);
	*/
		bit_shift = height * META_BLK_SHIFT;
		//printf("bit_shift %d\n", bit_shift);
		idx = blocknr >> bit_shift;
		//printf("idx %d\n",idx);
		bp = le64_to_cpu(node[idx]);
		//userfs_info("bp %lu, node[%d] %d\n", bp,idx,node[idx]);
		if (bp == 0)
			return 0;
		blocknr = blocknr & ((1 << bit_shift) - 1);
		height--;
	}

	//return bp; //最后一层leaf node 记录块地址
	//printf("height>0\n");
	//userfs_info("height %d\n",height);
	devid = bp >>62;
	if (devid >0){
		//printf("devid %d\n", devid);
		bmap_req->block_no =(bp & flag)>>g_block_size_shift;
		bmap_req->dev =devid +1;
		bmap_req->blk_count_found = 512;
		//userfs_info("bmap_req->block_no %ld\n", bmap_req->block_no);
		//printf("devid %d\n", devid+1);
		//offset值是多少
		bmap_req->start_offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);
		struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
		blocknode->devid = devid+1;
		blocknode->size =g_lblock_size_bytes;
		blocknode->addr = (bp & flag)>>g_block_size_shift;
		blocknode->fileoff = bmap_req->start_offset;
		//printf("blocknode->fileoff %d\n", blocknode->fileoff);
		pthread_rwlock_wrlock(&inode->blktree_rwlock);
		block_insert(&(inode->blktree),blocknode);
		pthread_rwlock_unlock(&inode->blktree_rwlock);
		return 0;
	}else{
		//printf("devid == 0\n");
		memcpy(node, mmapstart+bp, g_block_size_bytes);
		uint64_t key = (bmap_req->start_offset >> g_block_size_shift)%lblock_addr;
		offset = ALIGN_FLOOR(bmap_req->start_offset, g_lblock_size_bytes);

		for(int i=0;i<lblock_addr;i++){
			struct block_node *blocknode = (struct block_node *)malloc(sizeof(struct block_node));
			//userfs_info("offset %d\n", offset);
			devid = node[key]>>62;
			if(devid>0){
				blocknode->devid = devid+1;
				blocknode->addr = (node[i] & flag)>>g_block_size_shift;
			}else{
				blocknode->devid = devid+1;
				blocknode->addr = node[i] >>g_block_size_shift;
			}
			if(i==key){
				bmap_req->block_no =blocknode->addr;
				bmap_req->dev =blocknode->devid;
				bmap_req->blk_count_found = 1;
			}
			blocknode->fileoff = offset;
			blocknode->size =g_block_size_bytes;
			offset +=g_block_size_bytes;
			pthread_rwlock_wrlock(&inode->blktree_rwlock);
			block_insert(&(inode->blktree),blocknode);
			pthread_rwlock_unlock(&inode->blktree_rwlock);

		}
		/*
		//printf("key %d\n", key);
		devid = node[key]>>62;
		//printf("devid %d\n", devid);
		if(devid>0){
			bmap_req->block_no =(node[key] &flag)>>g_block_size_shift;
			bmap_req->dev =devid +1;
			blocknode->devid = devid+1;
			blocknode->addr = (node[key] &flag) >>g_block_size_shift;
			userfs_info("bmap_req->block_no %ld\n", bmap_req->block_no);
			//printf("devid %d\n", devid+1);
		}else{
			bmap_req->block_no =node[key] >>g_block_size_shift;
			bmap_req->dev =devid +1;
			
			blocknode->devid = devid+1;
			blocknode->addr = node[key] >>g_block_size_shift;
			userfs_info("bmap_req->block_no %ld\n", bmap_req->block_no);
			//printf("devid %d\n", devid+1);
		}
		blocknode->fileoff = bmap_req->start_offset;
		bmap_req->blk_count_found = 1;
		blocknode->size =g_block_size_bytes;
		userfs_info("blocknode->fileoff %d\n", blocknode->fileoff);
		pthread_rwlock_wrlock(&inode->blktree_rwlock);
		block_insert(&(inode->blktree),blocknode);
		pthread_rwlock_unlock(&inode->blktree_rwlock);
		*/
		return 0;
	}

}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
int itrunc(struct inode *ip, offset_t length)
{
	int ret = 0;
	struct buffer_head *bp;
	offset_t size;
	struct fcache_block *fc_block;
	offset_t key;

	userfs_assert(ip);
	userfs_assert(ip->itype == T_FILE);

	if (length == 0) {
		fcache_del_all(ip);
	} else if (length < ip->size) {
		/* invalidate all data pointers for log block.
		 * If libfs only takes care of zero trucate case, 
		 * dropping entire hash table is OK. 
		 * It considers non-zero truncate */
		for (size = ip->size; size > 0; size -= g_block_size_bytes) {
			key = (size >> g_block_size_shift);

			fc_block = fcache_find(ip, key);
			if (fc_block) {
				if (fc_block->is_data_cached)
					list_del(&fc_block->l);
				fcache_del(ip, fc_block);
				userfs_free(fc_block);
			}

		}
	} 

	pthread_mutex_lock(&ip->i_mutex);

	ip->size = length;

	pthread_mutex_unlock(&ip->i_mutex);

	userfs_get_time(&ip->mtime);

	//iupdate(ip);

	return ret;
}

void stati(struct inode *ip, struct stat *st)
{
	userfs_assert(ip);

	//st->st_dev = ip->dev;
	st->st_ino = ip->inum;
	st->st_mode = 0;
	st->st_nlink = ip->nlink;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = ip->size;
	st->st_blksize = g_block_size_bytes;
	// This could be incorrect if there is file holes.
	st->st_blocks = ip->size >>g_block_size_shift;

	st->st_mtime = (time_t)ip->mtime.tv_sec;
	st->st_ctime = (time_t)ip->ctime.tv_sec;
	st->st_atime = (time_t)ip->atime.tv_sec;
	ip->i_ref--;
}

// TODO: Now, eviction is simply discarding. Extend this function
// to evict data to the update log.
static void evict_read_cache(struct inode *inode, uint32_t n_entries_to_evict)
{
	uint32_t i = 0;
	struct fcache_block *_fcache_block, *tmp;

	list_for_each_entry_safe_reverse(_fcache_block, tmp, 
			&g_fcache_head.lru_head, l) {
		if (i > n_entries_to_evict) 
			break;

		if (_fcache_block->is_data_cached) {
			g_fcache_head.n-=_fcache_block->size >>g_block_size_shift;
			i+=_fcache_block->size >>g_block_size_shift;
			userfs_free(_fcache_block->data);

			if (!_fcache_block->log_addr) {
				list_del(&_fcache_block->l);
				fcache_del(inode, _fcache_block);
				userfs_free(_fcache_block);
			}

			

		}
	}
}

// Note that read cache does not copying data (parameter) to _fcache_block->data.
// Instead, _fcache_block->data points memory in data. 
static struct fcache_block *add_to_read_cache(struct inode *inode, 
		offset_t off, uint32_t size, uint8_t *data)
{	
	//struct timeval start, end;
	//gettimeofday(&start, NULL);
    //userfs_info("start sec %d, usec %d\n", start.tv_sec, start.tv_usec);

	//printf("add_to_read_cache off %d, size %d\n", off,size);
	struct fcache_block *_fcache_block;
	//printf("offset %d\n", off);
	_fcache_block = fcache_find(inode, (off >> g_block_size_shift));
	//userfs_info("add_to_read_cache _fcache_block %lu, off %d, size %d\n",_fcache_block, off,size);
	if (!_fcache_block) {
		_fcache_block = fcache_alloc_add(inode, (off >> g_block_size_shift), 0); 
		g_fcache_head.n+=(size >> g_block_size_shift);
		//printf("g_fcache_head.n %d\n", g_fcache_head.n);
	}else {
		//userfs_info("delete _fcache_block off %d, size %d, _fcache_block->size %d\n",off,size,_fcache_block->size);
		if(_fcache_block->size<g_block_size_bytes || _fcache_block->size >((uint64_t)0x1<<g_lblock_size_shift)){

			fcache_del(inode,_fcache_block);
		}
	}

	_fcache_block->is_data_cached = 1;
	_fcache_block->data = data;
	_fcache_block->size =size;
	//userfs_info("_fcache_block->size %d\n",_fcache_block->size);
	//userfs_info("(_fcache_block->l %lu, g_fcache_head.lru_head %lu\n",&_fcache_block->l,&g_fcache_head.lru_head);
	list_move(&_fcache_block->l, &g_fcache_head.lru_head); 
	//printf("list_move\n");
	if (g_fcache_head.n > g_max_read_cache_blocks) {
		userfs_info("evict_read_cache size %d, g_max_read_cache_blocks %d\n",g_fcache_head.n, g_max_read_cache_blocks);
		evict_read_cache(inode, g_fcache_head.n - g_max_read_cache_blocks);
		//userfs_info("evict_read_cache size %d\n",g_fcache_head.n);
	}
	//userfs_info("_fcache_block %lu\n",_fcache_block);
	//gettimeofday(&end, NULL);
    //printf("add_to_read_cache use time %d usec\n", end.tv_usec - start.tv_usec);
	return _fcache_block;
}

/*
int check_log_invalidation(struct fcache_block *_fcache_block)
{
	int ret = 0;
	int version_diff = g_fs_log->avail_version - _fcache_block->log_version;

	userfs_assert(version_diff >= 0);

	// fcache must be used for either data cache or log address caching.
	// userfs_assert((_fcache_block->is_data_cached && _fcache_block->log_addr) == 0);

	pthread_rwlock_wrlock(invalidate_rwlock);

	// fcache is used for log address.
	if ((version_diff > 1) || 
			(version_diff == 1 && 
			 _fcache_block->log_addr < g_fs_log->next_avail_header)) {
		userfs_debug("invalidate: inum %u offset %lu -> addr %lu\n", 
				ip->inum, _off, _fcache_block->log_addr);
		_fcache_block->log_addr = 0;

		// Delete fcache_block when it is not used for read cache.
		if (!_fcache_block->is_data_cached) 
			ret = 1;
		else
			ret = 0;
	}

	pthread_rwlock_unlock(invalidate_rwlock);

	return ret;
}
*/

/*读取数据
*先查cache中是否有数据
*如果没有，这需要根据inode中的地址到设备上找对应的数据
* 查找 inode上 rb_root blktree;   //index blk for file，去找块地址
*如果blktree中存在对应块地址，这根据块地址去对应位置读取
*否则，读取对应inode，找到访问btree树，找到对应的地址，读取数据，
*并将地址index到blktree中。
*/




int do_unaligned_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	//userfs_printf("do_unaligned_read off %d, io_size %d\n",off, io_size);
	//printf("dst %p\n", dst);
	int io_done = 0, ret;
	uint8_t devid=1;
	offset_t key, off_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	unsigned long bfileoff,rfileoff,readsize;
	//struct list_head io_list_log;
	bmap_req_t bmap_req;

	//INIT_LIST_HEAD(&io_list_log);

	userfs_assert(io_size < g_block_size_bytes);

	key = (off >> g_block_size_shift);
	offset_t MB_key = (off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	//printf("key %d\n", key);
	//printf("MB_key %d\n", MB_key);
	
	off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);
	//printf("off_aligned %d\n", off_aligned);
	offset_t off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);

	//printf("offset_aligned %d, offset_2M_aligned %d\n",off_aligned,off_MB_aligned);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	_fcache_block = fcache_find(ip, key);

	//userfs_info("_fcache_block %p\n", _fcache_block);
	if (enable_perf_stats) {
		g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.l0_search_nr++;
	}
/*
	if (_fcache_block) {
		//ret = check_log_invalidation(_fcache_block);
		if (ret) {
			fcache_del(ip, _fcache_block);
			userfs_free(_fcache_block);
			_fcache_block = NULL;
		}
	}	
*/
	if (_fcache_block) {
		// read cache hit
		//printf("read cache hit\n");
		if (_fcache_block->is_data_cached && _fcache_block->size >=(off-off_aligned+io_size)) {
			memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			list_move(&_fcache_block->l, &g_fcache_head.lru_head);

			return io_size;
		} 
		// 如果沒cache数据，则表明cache了nvm中间
		else if (_fcache_block->log_addr) {
			addr_t block_no = _fcache_block->log_addr;

			userfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", 
					block_no, off, off, io_size);
/*
			if(block_no > NVMBLOCK){ //block_no 是什么地址
				block_no = block_no-NVMBLOCK;
				devid=2;
			}
			*/
			bh = bh_get_sync_IO(devid, block_no, BH_NO_DATA_ALLOC);

			bh->b_offset = off - off_aligned;
			bh->b_data = dst;
			bh->b_size = io_size;
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
			goto do_io_unaligned;
			//list_add_tail(&bh->b_io_list, &io_list_log);
		}else{
			userfs_info("clear fcache_block %d, _fcache_block->size %d, size %d\n",key,_fcache_block->size, off-off_aligned+io_size);
		}
	}else if(MB_key!=key){
		_fcache_block = fcache_find(ip, MB_key);
		//printf("1fcache_blockr data + off %ld\n", _fcache_block->data +208896);
		//printf("_fcache_block %lu\n", _fcache_block);
		//if(_fcache_block)
			//printf("_fcache_block size %d\n", _fcache_block->size);
		//printf("_fcache_block bytes size %d\n", (_fcache_block->size <<g_block_size_shift));
		//printf("size %d\n", (off - off_MB_aligned + io_size));
		if (_fcache_block && _fcache_block->size>=(off - off_MB_aligned + io_size)) {
		// read cache hit
			//printf("read cache hit %d\n",_fcache_block->is_data_cached);
			//userfs_info("size %d, (off - off_MB_aligned + io_size) %d\n", _fcache_block->size,(off - off_MB_aligned + io_size));
			if (_fcache_block->is_data_cached) {
				//printf("_fcache_block->data %p\n", _fcache_block->data);
				//printf("4fcache_blockr data + off %ld\n", _fcache_block->data +208896);
				//printf("_fcache_block->data %s\n", (char *)_fcache_block->data);
				//printf("_fcache_block->data strlen %d\n", strlen((char *)_fcache_block->data));
				//printf("off %d, off_aligned %d\n", off, off_MB_aligned);
				//printf("off-off_MB_aligned %d\n", (off - off_MB_aligned));
				//printf("5fcache_blockr data + off %p\n", _fcache_block->data +(off - off_MB_aligned));
				//printf("6fcache_blockr data + off %p\n", _fcache_block->data +(off - off_MB_aligned)+io_size);
				//printf("dst %p\n", dst);
				memmove(dst, _fcache_block->data + (off - off_MB_aligned), io_size);
				//printf("memmove\n");
				list_move(&_fcache_block->l, &g_fcache_head.lru_head);
				//printf("list_move\n");
				//userfs_info("return io_size %d\n",io_size);
				return io_size;
			} 
			// 如果沒cache数据，则表明cache了nvm中间
			else if (_fcache_block->log_addr) {
				//printf("_fcache_block->log_addr\n");
				addr_t block_no = _fcache_block->log_addr;
				//userfs_info("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", block_no, off, off, io_size);
				userfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", 
						block_no, off, off, io_size);
	/*
				if(block_no > NVMBLOCK){ //block_no 是什么地址
					block_no = block_no-NVMBLOCK;
					devid=2;
				}
				*/
				bh = bh_get_sync_IO(devid, block_no, BH_NO_DATA_ALLOC);

				bh->b_offset = off - off_MB_aligned;
				bh->b_data = dst;
				bh->b_size = io_size;

				bh_submit_read_sync_IO(bh);
				bh_release(bh);
				goto do_io_unaligned;
				//list_add_tail(&bh->b_io_list, &io_list_log);
			}else{
				userfs_info("clear fcache_block %d\n",MB_key);
			}
		}
	}

	//printf("block_search\n");
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),off_aligned);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	//userfs_info("block_node %lu\n",blocknode);
	//printf("blocknode->size %d\n", blocknode->size);
	if(blocknode!=NULL){
		//userfs_info("0blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
		//userfs_info("off %d, off_aligned %d\n", off, off_aligned);
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(bfileoff >= rfileoff){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			//addr有没有问题
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr +(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			//printf("bh_get_sync_IO\n");
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = malloc(readsize);
			//printf("add_to_read_cache\n");
			_fcache_block = add_to_read_cache(ip, off_aligned, readsize, bh->b_data);
			//printf("bh_submit_read_sync_IO\n");
			bh_submit_read_sync_IO(bh);
			//printf("bh->b_data %s\n", bh->b_data);
			bh_release(bh);
			//userfs_info("_fcache_block->data %p\n", _fcache_block->data);
			memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			//userfs_info("dst %p\n", dst);
			goto do_io_unaligned;
		}
	}else if(off_aligned!=off_MB_aligned){
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		//printf("1block_node %lu\n",blocknode);
		//userfs_info("2blocknode->size %d\n", blocknode->size);
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(blocknode!=NULL){
			userfs_info("1blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
		}
		if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			//userfs_info("1blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
			//addr有没有问题
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr+(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = malloc(readsize);
			_fcache_block = add_to_read_cache(ip, off_aligned, readsize, bh->b_data);
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
			memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			goto do_io_unaligned;
		}
	}

	// global shared area search
	bmap_req.start_offset = off_aligned;
	bmap_req.blk_count_found = 0;
	bmap_req.blk_count = 1;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Get block address from shared area.
	//printf("bmap1\n");
	ret = bmap(ip, &bmap_req);
	//userfs_info("bmap bmap_req->block_no %d, bmap_req->dev %d\n",bmap_req.block_no,bmap_req.dev);
	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO)
		goto do_io_unaligned;

	bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
	bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

	// NVM case: no read caching.
	if (bmap_req.dev == g_root_dev) {
		bh->b_offset = off - off_aligned;
		bh->b_data = malloc(g_block_size_bytes);
		bh->b_size = io_size;

		_fcache_block = add_to_read_cache(ip, off_aligned, g_block_size_bytes, bh->b_data);

		bh_submit_read_sync_IO(bh);
		bh_release(bh);

		//需要memmove吗
		memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
	} 
	// SSD and HDD cache: do read caching.
	else {
//		userfs_assert(_fcache_block == NULL);

#if 0
		// TODO: Move block-level readahead to read cache
		if (bh->b_dev == g_ssd_dev)
			userfs_readahead(g_ssd_dev, bh->b_blocknr, (128 << 10));
#endif

		bh->b_data = malloc(g_lblock_size_bytes);
		bh->b_size = g_lblock_size_bytes;
		bh->b_offset = 0;

		_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);


		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		bh_submit_read_sync_IO(bh);

		//userfs_io_wait(g_ssd_dev, 1);

		if (enable_perf_stats) {
			g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.read_data_nr++;
		}

		bh_release(bh);

		// copying cached data to user buffer
		memmove(dst, _fcache_block->data + (off - off_MB_aligned), io_size);
	}

do_io_unaligned:
	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Patch data from log (L0) if up-to-date blocks are in the update log.
	// This is required when partial updates are in the update log.

	//作用是什么
	/*
	list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}
	*/
	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
	//userfs_info("unread finish io_size %d\n",io_size);
	return io_size;
}

/*读取数据
*先查cache中是否有数据
*如果没有，这需要根据inode中的地址到设备上找对应的数据
* 查找 inode上 rb_root blktree;   //index blk for file，去找块地址
*如果blktree中存在对应块地址，这根据块地址去对应位置读取
*否则，读取对应inode，找到访问btree树，找到对应的地址，读取数据，
*并将地址index到blktree中。
*/


int do_aligned_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_info("do_aligned_read off %d,io_size %d\n",off,io_size);
	//printf("dst %p\n", dst);
	struct timeval start, end;
	gettimeofday(&start, NULL);
    userfs_info("start sec %d, usec %d\n", start.tv_sec, start.tv_usec);


	int io_to_be_done = 0, ret, i;
	uint8_t devid =1;
	uint64_t l=0, list_size;
	offset_t key, _off, pos;
	offset_t MB_key, pMB_key=1073741824UL, off_MB_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list, io_list_log;
	uint32_t read_size, buf_size, remain_size = io_size;
	uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
	struct cache_copy_list copy_list[bitmap_size];
	bmap_req_t bmap_req;


	//return io_size;
	//userfs_info("bitmap_size is %d\n", bitmap_size);
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	INIT_LIST_HEAD(&io_list_log);

	userfs_assert(io_size % g_block_size_bytes == 0);

	for (pos = 0, _off = off; pos < io_size; ) {
		key = (_off >> g_block_size_shift);
		MB_key = (_off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	
		//off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);

		//off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);
		//printf("pos %d, io_size %d\n", pos, io_size);
		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		//_fcache_block = fcache_find(ip, MB_key);

		if (enable_perf_stats) {
			g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.l0_search_nr++;
		}
/*
		if (_fcache_block) {
			//ret = check_log_invalidation(_fcache_block);
			if (ret) {
				fcache_del(ip, _fcache_block);
				userfs_free(_fcache_block);
				_fcache_block = NULL;
			}
		}

*/		
		//printf("key %d, MB_key %d, pMB_key %d\n",key,MB_key,pMB_key );
		if(pMB_key!=MB_key){
			//printf("pMB_key!=MB_key\n");
			_fcache_block = fcache_find(ip, MB_key);
			//printf("_fcache_block %p\n", _fcache_block);
			off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);
			pMB_key=MB_key;
			//printf("_fcache_block %p\n", _fcache_block);
			
			if (_fcache_block && _fcache_block->size>=(off - off_MB_aligned + g_block_size_bytes)) {
				
				// read cache hit
				//printf("1_fcache_block->data %p\n", _fcache_block->data);
				//printf("1_fcache_block->data size %d\n", strlen(_fcache_block->data));
				//printf("1_fcache_block->data %s\n", (char *)(_fcache_block->data));
				if (_fcache_block->is_data_cached) {
					buf_size = _fcache_block->size;//&(~(((unsigned long)0x1<<g_block_size_shift) -1));
					//userfs_info("0_fcache_block->size %d, buf_size %d, remain_size %d\n",_fcache_block->size,buf_size,remain_size);
					read_size = remain_size < buf_size ? remain_size: buf_size;
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
						//printf("copy_list1\n");
						//printf("copy_list[%d]\n", l);
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}

					//printf("2fcache_blockr data + off %ld\n", _fcache_block->data +208896);
					//printf("2_fcache_block->data %s\n", (char *)_fcache_block->data);
					// move the fcache entry to head of LRU
					list_move(&_fcache_block->l, &g_fcache_head.lru_head);
					//userfs_info("0_fcache_block->size %d, bitmap_clear pos %d, read_size %d\n", _fcache_block->size,(pos >> g_block_size_shift),read_size>>g_block_size_shift);
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
					//userfs_info("read cache hit: offset %lu(0x%lx) size %u\n", off, off, io_size);
					userfs_debug("read cache hit: offset %lu(0x%lx) size %u\n", 
								off, off, io_size);
				} 
				// the update log search
				else if (_fcache_block->log_addr) {
					addr_t block_no = _fcache_block->log_addr;
					//userfs_info("GET from update log: blockno %lx offset %lu(0x%lx) size %lu\n", block_no, off, off, io_size);
					userfs_debug("GET from update log: blockno %lx offset %lu(0x%lx) size %lu\n", 
							block_no, off, off, io_size);

	/*				if(block_no > NVMBLOCK){ //block_no 是什么地址
						block_no = block_no-NVMBLOCK;
						devid=2;
					}
	*/
					bh = bh_get_sync_IO(1, block_no, BH_NO_DATA_ALLOC);

					bh->b_offset = off - off_MB_aligned;
					bh->b_data = malloc(_fcache_block->size);
					bh->b_size = _fcache_block->size;

					list_add_tail(&bh->b_io_list, &io_list_log);
					read_size = remain_size < _fcache_block->size ? remain_size: _fcache_block->size;
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
					//bitmap_clear(io_bitmap, (pos >> g_block_size_shift), _fcache_block->size >>g_block_size_shift);
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
				}
			}
		}else if(MB_key!=key){
			//printf("MB_key!=key\n");
			_fcache_block = fcache_find(ip,key);
			if(_fcache_block){
				
				//printf("2_fcache_block->data %p\n", _fcache_block->data);
				if (_fcache_block->is_data_cached && _fcache_block->size >=g_block_size_bytes) {
					//???为什么向上取整 buf_size = (_fcache_block->size+g_block_size_bytes-1)&(~(((unsigned long)0x1<<g_block_size_shift) -1));
					buf_size = _fcache_block->size&(~(((unsigned long)0x1<<g_block_size_shift) -1));
					//userfs_info("1_fcache_block->size %d, buf_size %d, remain_size %d\n",_fcache_block->size,buf_size,remain_size);
					read_size = remain_size < buf_size ? remain_size: buf_size;
					//for (; l < (_fcache_block->size>>g_block_size_shift); l++){
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
						//printf("copy_list1\n");
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}

					// move the fcache entry to head of LRU
					list_move(&_fcache_block->l, &g_fcache_head.lru_head);
					//userfs_info("0 pos %d\n",pos);
					//userfs_info("1_fcache_block->size %d, bitmap_clear pos %d, _off %d, read_size %d, remain_size %d\n", _fcache_block->size, (pos >> g_block_size_shift), _off, read_size>>g_block_size_shift,remain_size);
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;

					//bitmap_clear(io_bitmap, (pos >> g_block_size_shift), _fcache_block->size >>g_block_size_shift);
				
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
					//list_move(&_fcache_block->l, &g_fcache_head.lru_head);

					//return io_size;
				} 
				// 如果沒cache数据，则表明cache了nvm中间
				else if (_fcache_block->log_addr) {
					addr_t block_no = _fcache_block->log_addr;
					//userfs_info("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", block_no, off, off, io_size);
					userfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", block_no, off, off, io_size);
					/*
					if(block_no > NVMBLOCK){ //block_no 是什么地址
						block_no = block_no-NVMBLOCK;
						devid=2;
					}
					*/
					bh = bh_get_sync_IO(1, block_no, BH_NO_DATA_ALLOC);

					bh->b_offset = 0;
					bh->b_data = malloc(_fcache_block->size);
					bh->b_size = _fcache_block->size;

					list_add_tail(&bh->b_io_list, &io_list_log);
					read_size = remain_size < _fcache_block->size ? remain_size: _fcache_block->size;
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
					//bitmap_clear(io_bitmap, (pos >> g_block_size_shift), _fcache_block->size >>g_block_size_shift);
				
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
				}

			}

		}
		if(_fcache_block){
			pos += buf_size;
			_off += buf_size;
			//userfs_info("_fcache_block->size %d, remain_size %d, buf_size %d\n",_fcache_block->size,remain_size,buf_size);
			//userfs_info("buf_size %d\n",buf_size);
		}else{
			pos += g_block_size_bytes;
			_off += g_block_size_bytes;
		}
		//userfs_info("1 pos %d\n",pos);
	}

	//userfs_info("remain_size %d\n",remain_size);
	//userfs_info("bitmap_weight(io_bitmap, bitmap_size) %d\n", bitmap_weight(io_bitmap, bitmap_size));
	// All data come from the update log.
	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
		}
		//printf("3fcache_blockr data + off %ld\n", _fcache_block->data +208896);
		//printf("3_fcache_block->data %s\n", (char *)_fcache_block->data);
		//printf("return io_size %d\n", io_size);
		for (i = 0 ; i < bitmap_size; i++) {
			//printf("i %d, bitmap_size %d\n", i,bitmap_size);
			//printf("copy_list[%d] %p\n", i, &copy_list[i]);
			//sleep(5);
			
			//printf("copy_list[%d].dst_buffer %p\n",i, copy_list[i].dst_buffer);
			if (copy_list[i].dst_buffer != NULL) {
				//printf("copy_list[%d].dst_buffer %s\n",i, copy_list[i].dst_buffer);
				//printf("copy_list[%d].cached_data %p\n",i, copy_list[i].cached_data);
				//printf("copy_list[%d].size %d\n",i, copy_list[i].size);
				memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
				//printf("dst_buffer %s\n, io_size %d\n", (char *)copy_list[i].dst_buffer);
				if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
					panic("read request overruns the user buffer\n");
			}
		
		}
		return io_size;
	}

	//userfs_info("do_global_search remain_size %d\n",remain_size);
	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
do_global_search:
	userfs_info("do_global_search %d\n",remain_size);
	_off = off + (find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift);
	pos = find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift;
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	userfs_info("_off %d, pos %d, bitmap_pos %d\n", _off,pos,bitmap_pos);
	
	off_MB_aligned=ALIGN_FLOOR(off, g_lblock_size_bytes);

	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	gettimeofday(&end, NULL);
    printf("1do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

	userfs_info("block_node is %p, remain_size %d, off %d\n", blocknode,remain_size,_off);
	if(blocknode!=NULL){
		//printf("remain_size %d\n", remain_size);
		
		if(blocknode->size<g_block_size_bytes && remain_size> blocknode->size){
			blocknode->size=g_block_size_bytes;
			goto read_from_device;
		}
		buf_size = blocknode->size &(~(((unsigned long)0x1<<g_block_size_shift) -1));
		userfs_info("block_node is fileoff %d,addr %ld,size %d, buf_size %d\n", blocknode->fileoff, blocknode->addr,blocknode->size, buf_size);
		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
		//bh->b_size=blocknode->size;
		bh->b_size =buf_size;
		bh->b_offset = 0;
		gettimeofday(&end, NULL);
   		printf("10do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);
   		printf("malloc buf_size %d\n", buf_size);
		bh->b_data = (uint8_t *)malloc(io_size);
		//bh->b_data =dst;
		gettimeofday(&end, NULL);
   		printf("11do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);
		userfs_info("_fcache_block bh->b_data %lu\n",bh->b_data);
		_fcache_block = add_to_read_cache(ip, _off, buf_size, bh->b_data);
		read_size = remain_size < buf_size ? remain_size: buf_size;
		userfs_info("read_size %d, l %d\n", read_size,l);

		gettimeofday(&end, NULL);
   		printf("12do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

		for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
			//printf("copy_list %d\n",l);
			//printf("copy_list[%d], pos +(i<<g_block_size_shift) %d\n",l,pos +(i<<g_block_size_shift));
			copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
			copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
			copy_list[l].size = g_block_size_bytes;
		}

		list_add_tail(&bh->b_io_list, &io_list);
		//printf("list_add_tail\n");
		userfs_info("bitmap_clear (pos >> g_block_size_shift) %d,  read_size %d\n", (pos >> g_block_size_shift),read_size>>g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
		io_to_be_done += buf_size>>g_block_size_shift;

		//goto do_global_search;
		//userfs_info("goto %d, bitmapweight %d\n",bitmap_weight(io_bitmap, bitmap_size),bitmapweight);
		//printf("read bitmapweight %d\n",bitmapweight);

		gettimeofday(&end, NULL);
   		//printf("2do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);


		if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
			
			panic("read error\n");
		}
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
			goto do_global_search;
		}
		goto do_io_aligned;

	}else{
		if(_off!=off_MB_aligned){
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			//printf("blocknode->size %d\n", blocknode->size);
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size &(~(((unsigned long)0x1<<g_block_size_shift) -1));
				//userfs_info("blocknode->size %d, buf_size %d\n", blocknode->size,buf_size);
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				bh->b_data = malloc(buf_size);

				list_add_tail(&bh->b_io_list, &io_list);

				_fcache_block = add_to_read_cache(ip, off_MB_aligned, buf_size, bh->b_data);
				read_size = remain_size < buf_size ? remain_size: buf_size;
				for (i = 0; i < (read_size >>g_block_size_shift); i++,l++){
				//for (l = 0; l < (blocknode->size>>g_block_size_shift); l++){
					//userfs_info("copy_list read_size i %d\n",i);
					copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
					copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
					copy_list[l].size = g_block_size_bytes;
				}
				//bh_submit_read_sync_IO(bh);
				//bh_release(bh);
				//userfs_info("bitmap_clear (pos >> g_block_size_shift) %d, read_size %d\n", (pos >> g_block_size_shift),read_size>>g_block_size_shift);
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
				//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
				io_to_be_done += buf_size>>g_block_size_shift;

				//goto do_global_search;
				//printf("goto %d\n",bitmap_weight(io_bitmap, bitmap_size));
				//if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
				if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
					userfs_info("read error bitmapweight %d\n",bitmapweight);
					panic("read error\n");
				}
				if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
					bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
					goto do_global_search;
				}
				goto do_io_aligned;
			}
		}
	}
	//printf("bmap_req\n");
	// global shared area search
read_from_device:	
	bmap_req.start_offset = _off;
	bmap_req.blk_count = 
		find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos;
	bmap_req.dev = 0;
	bmap_req.block_no = 0;
	bmap_req.blk_count_found = 0;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Get block address from shared area.
	//printf("bmap2\n");
	ret = bmap(ip, &bmap_req);

	//userfs_info("bmap() bmap_req->block_no %ld, dev %d\n",bmap_req.block_no,bmap_req.dev);
	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO) {
		if (bmap_req.blk_count_found != bmap_req.blk_count) {
			//panic("could not found blocks in any storage layers\n");
			userfs_debug("inum %u - count not find block in any storage layer\n", 
					ip->inum);
		}
		goto do_io_aligned;
	}

	// NVM case: no read caching.
	//userfs_info("bmap_req.dev %d\n", bmap_req.dev);
	if (bmap_req.dev == g_root_dev) {
		//printf("g_root_dev\n");
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	} 
	// SSD and HDD cache: do read caching.
	else {
		//printf("g_ssd_dev\n");
		

#if 0
		// TODO: block-level read_ahead to read cache.
		if (bh->b_dev == g_ssd_dev)
			userfs_readahead(g_ssd_dev, bh->b_blocknr, (256 << 10));
#endif

		 /* The read cache is managed by 4 KB block.
		 * For large IO size (e.g., 256 KB), we have two design options
		 * 1. To make a large IO request to SSD. But, in this case, libfs
		 *    must copy IO data to read cache for each 4 KB block.
		 * 2. To make 4 KB requests for the large IO. This case does not
		 *    need memory copy; SPDK could make read request with the 
		 *    read cache block.
		 * Currently, I implement it with option 2
		 */

		// register IO memory to read cache for each 4 KB blocks.
		// When bh finishes IO, the IO data will be in the read cache.
		//printf("bmap_req.blk_count_found %d\n", bmap_req.blk_count_found);
		//for (cur = _off, l = 0; l < bmap_req.blk_count_found; 
		//		cur += g_lblock_size_bytes) {
			//printf("off %d, block %d\n",cur,l);
			bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
			bh->b_data = malloc(bmap_req.blk_count_found << g_block_size_shift);
			bh->b_size = bmap_req.blk_count_found << g_block_size_shift;
			bh->b_offset = 0;

			read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
			//userfs_info("read_size %d\n", read_size);
			//printf("bh->data %p\n", bh->b_data);
			if(bmap_req.blk_count_found==512){
				_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);
				for (i = 0; i < (read_size >> g_block_size_shift);i++, l++){
					//printf("copy_list3\n");
			//		cur += g_lblock_size_bytes) {
					//printf("l %d\n", l);
					copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
					copy_list[l].cached_data = _fcache_block->data+(i<<g_block_size_shift);
					copy_list[l].size = g_block_size_bytes;
					//printf("copy_list[%d] %p\n", l,&copy_list[l]);
					//printf("copy_list[%d].dst_buffer %p, pos %d\n", l,copy_list[l].dst_buffer,pos);
				}
			}else if(bmap_req.blk_count_found==1){
				_fcache_block = add_to_read_cache(ip, _off, g_block_size_bytes, bh->b_data);
				copy_list[l].dst_buffer = dst + pos;
				copy_list[l].cached_data = _fcache_block->data;
				copy_list[l].size = g_block_size_bytes;

				//printf("copy_list[%d].dst_buffer %p, pos %d\n",l, copy_list[l].dst_buffer,pos);
				
				l++;
				
			}else{
				printf("unkonwn error\n");
			}
			

			/*
			for (i = 0; i < (read_size >> g_block_size_shift);i++, l++){
				//printf("copy_list3\n");
		//		cur += g_lblock_size_bytes) {
				printf("l %d\n", l);
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data+(l<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
				//printf("copy_list[%d] %p\n", l,&copy_list[l]);
				//printf("copy_list[%d].dst_buffer %p\n", l,copy_list[l].dst_buffer);
			}
			*/
			//l+=512;
		//}
		//printf("_fcache_block->size %d\n", _fcache_block->size);
		
		list_add_tail(&bh->b_io_list, &io_list);
	}
	/* EAGAIN happens in two cases:
	 * 1. A size of extent is smaller than bmap_req.blk_count. In this 
	 * case, subsequent bmap call starts finding blocks in next extent.
	 * 2. A requested offset is not in the L1 tree. In this case,
	 * subsequent bmap call starts finding blocks in other lsm tree.
	 */
	//userfs_info("ret %d\n",ret);
	if (ret == -EAGAIN) {
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		//bitmap_clear(io_bitmap, bitmap_pos, bmap_req.blk_count_found);
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {

		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		//bitmap_clear(io_bitmap, bitmap_pos, bmap_req.blk_count_found);

		io_to_be_done += bmap_req.blk_count_found;

		//userfs_assert(bitmap_weight(io_bitmap, bitmap_size) == 0);
		//printf("bitmap_weight(io_bitmap, bitmap_size) %d\n", bitmap_weight(io_bitmap, bitmap_size));
		if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
			userfs_info("read error bitmapweight %d\n",bitmapweight);
			panic("read error\n");
		}
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			bitmapweight= bitmap_weight(io_bitmap, bitmap_size);

		//if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			//printf("goto do_global_search\n");
			goto do_global_search;
		}

	}

	//printf("bitmap_weight %d\n",bitmap_weight(io_bitmap, bitmap_size));
	//userfs_info("io_to_be_done %d, io_size %d\n", io_to_be_done, io_size);
	userfs_assert(io_to_be_done >= (io_size >> g_block_size_shift));

do_io_aligned:
	//userfs_assert(bitmap_weight(io_bitmap, bitmap_size) == 0);
	//gettimeofday(&end, NULL);
    //printf("3do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();
	//printf("list_for_each_entry_safe\n");
	// Read data from L1 ~ trees
	list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {
	//list_for_each_entry(bh, &io_list, b_io_list) {
		//printf("bh->offset %d\n", bh->b_offset);
		userfs_info("bh->b_size %d\n",bh->b_size);
		bh_submit_read_sync_IO(bh);
		//userfs_info("bh->b_data %lu\n", bh->b_data);
		bh_release(bh);
	}

	
	//printf("bh_release\n");
	//userfs_io_wait(g_ssd_dev, 1);
	// At this point, read cache entries are filled with data.

	// copying read cache data to user buffer.
	//userfs_info("bitmap_size %d\n", bitmap_size);
	//gettimeofday(&end, NULL);
    //printf("4do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

	for (i = 0 ; i < bitmap_size; i++) {
		//printf("i %d, bitmap_size %d\n", i,bitmap_size);
		//printf("copy_list[%d] %p\n", i, &copy_list[i]);
		//sleep(5);
		
		//printf("copy_list[%d].dst_buffer %p\n",i, copy_list[i].dst_buffer);
		if (copy_list[i].dst_buffer != NULL) {
			//printf("copy_list[%d].dst_buffer %s\n",i, copy_list[i].dst_buffer);
			//printf("copy_list[%d].cached_data %p\n",i, copy_list[i].cached_data);
			//printf("copy_list[%d].size %d\n",i, copy_list[i].size);
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
			//printf("copy_list[%d].dst_buffer %p\n",i, copy_list[i].dst_buffer);
			//printf("copy_list[%d].cached_data %p\n",i, copy_list[i].cached_data);
			//printf("copy_list[%d].size %d\n",i, copy_list[i].size);
			//printf("dst_buffer %s\n, io_size %d\n", (char *)copy_list[i].dst_buffer);
			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
		
	}


	//printf("dst_buffer %s\n, io_size %d\n", (char *)copy_list[0].dst_buffer);

	//printf("_fcache_block->data %s\n", (char *)(_fcache_block->data+io_size-10));

	//printf("_fcache_block->data %p\n", _fcache_block->data);

	//printf("_fcache_block %p\n", _fcache_block);
	//panic("read request overruns the user buffer\n");
	// Patch data from log (L0) if up-to-date blocks are in the update log.
	// This is required when partial updates are in the update log.
	/*
	list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}
	*/
	userfs_info("read finish io_size %d\n",io_size);
	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
	gettimeofday(&end, NULL);
	userfs_info("end   sec %d, usec %d\n", end.tv_sec, end.tv_usec);
    //printf("do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

	return io_size;
}




int do_unalign_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_info("do_unaligned_read off %d, io_size %d\n",off, io_size);
	//printf("dst %p\n", dst);
	int io_done = 0, ret;
	//uint8_t devid=1;
	offset_t key, off_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	unsigned long bfileoff,rfileoff,readsize;
	//struct list_head io_list_log;
	bmap_req_t bmap_req;

	//INIT_LIST_HEAD(&io_list_log);

	userfs_assert(io_size < g_block_size_bytes);

	//key = (off >> g_block_size_shift);
	//offset_t MB_key = (off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	//printf("key %d\n", key);
	//printf("MB_key %d\n", MB_key);
	
	off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);
	//printf("off_aligned %d\n", off_aligned);
	offset_t off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);

	//printf("offset_aligned %d, offset_2M_aligned %d\n",off_aligned,off_MB_aligned);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();


	//userfs_info("_fcache_block %p\n", _fcache_block);
	if (enable_perf_stats) {
		g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.l0_search_nr++;
	}


	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),off_aligned);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	if(blocknode!=NULL){
		userfs_info("0blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
		//userfs_info("off %d, off_aligned %d\n", off, off_aligned);
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(bfileoff >= rfileoff){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			//addr有没有问题
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr +(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			//printf("bh_get_sync_IO\n");
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = dst;
			//printf("add_to_read_cache\n");
			//_fcache_block = add_to_read_cache(ip, off_aligned, readsize, bh->b_data);
			//printf("bh_submit_read_sync_IO\n");
			bh_submit_read_sync_IO(bh);
			//printf("bh->b_data %s\n", bh->b_data);
			bh_release(bh);
			//userfs_info("_fcache_block->data %p\n", _fcache_block->data);
			//memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			//userfs_info("dst %p\n", dst);
			goto do_io_unalign;
		}
	}else if(off_aligned!=off_MB_aligned){
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		//printf("1block_node %lu\n",blocknode);
		//userfs_info("2blocknode->size %d\n", blocknode->size);
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(blocknode!=NULL){
			userfs_info("1blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
		}
		if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			//userfs_info("1blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
			//addr有没有问题
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr+(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = dst;
			//_fcache_block = add_to_read_cache(ip, off_aligned, readsize, bh->b_data);
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
			//memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			goto do_io_unalign;
		}
	}

	// global shared area search
	bmap_req.start_offset = off_aligned;
	bmap_req.blk_count_found = 0;
	bmap_req.blk_count = 1;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Get block address from shared area.
	//printf("bmap1\n");
	ret = bmap(ip, &bmap_req);
	userfs_info("bmap bmap_req->block_no %d, bmap_req->dev %d\n",bmap_req.block_no,bmap_req.dev);
	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO)
		goto do_io_unalign;

	bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
	bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

	// NVM case: no read caching.
	if (bmap_req.dev == g_root_dev) {
		bh->b_offset = off - off_aligned;
		bh->b_data = dst;
		bh->b_size = io_size;

		//_fcache_block = add_to_read_cache(ip, off_aligned, g_block_size_bytes, bh->b_data);

		bh_submit_read_sync_IO(bh);
		bh_release(bh);

		//需要memmove吗
		//memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
	} 
	// SSD and HDD cache: do read caching.
	else {


		bh->b_data = dst;
		bh->b_size = g_lblock_size_bytes;
		bh->b_offset = 0;

		//_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);


		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		bh_submit_read_sync_IO(bh);

		//userfs_io_wait(g_ssd_dev, 1);

		if (enable_perf_stats) {
			g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.read_data_nr++;
		}

		bh_release(bh);

		// copying cached data to user buffer
		//memmove(dst, _fcache_block->data + (off - off_MB_aligned), io_size);
	}

do_io_unalign:
	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Patch data from log (L0) if up-to-date blocks are in the update log.
	// This is required when partial updates are in the update log.

	//作用是什么
	/*
	list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}
	*/
	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
	userfs_info("unread finish io_size %d\n",io_size);
	return io_size;
}



int do_align_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_info("do_aligned_read off %d,io_size %d\n",off,io_size);
	//printf("dst %p\n", dst);
	struct timeval start, end;
	


	int io_to_be_done = 0, ret, i;
	//uint8_t devid =1;
	//uint64_t l=0, list_size;
	offset_t _off, pos;
	offset_t off_MB_aligned;
	//struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list;
	uint32_t read_size, buf_size, remain_size = io_size;
	uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
	//struct cache_copy_list copy_list[bitmap_size];
	bmap_req_t bmap_req;


	//return io_size;
	//userfs_info("bitmap_size is %d\n", bitmap_size);
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	//memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	//INIT_LIST_HEAD(&io_list_log);

	//userfs_assert(io_size % g_block_size_bytes == 0);

	//userfs_info("do_global_search remain_size %d\n",remain_size);
	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
do_global_search:
	userfs_info("do_global_search %d\n",remain_size);
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	pos = bitmap_pos << g_block_size_shift;
	_off = off + pos;
	
	
	userfs_info("_off %d, pos %d, bitmap_pos %d\n", _off,pos,bitmap_pos);
	
	//gettimeofday(&start, NULL);
    //userfs_info("start sec %d, usec %d\n", start.tv_sec, start.tv_usec);

	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	//gettimeofday(&end, NULL);
    //printf("1do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

	userfs_info("block_node is %p, remain_size %d, off %d\n", blocknode,remain_size,_off);
	if(blocknode!=NULL){
		//printf("remain_size %d\n", remain_size);
		/*
		if(blocknode->size<g_block_size_bytes && remain_size> blocknode->size){
			blocknode->size=g_block_size_bytes;
			goto read_from_device;
		}
		*/
		buf_size = blocknode->size & FFF00;
		userfs_info("block_node is fileoff %d,addr %ld,size %d, buf_size %d\n", blocknode->fileoff, blocknode->addr,blocknode->size, buf_size);
		
		//gettimeofday(&end, NULL);
   		//printf("10do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr + ((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
		//bh->b_size=blocknode->size;
		bh->b_size =buf_size;
		bh->b_offset = 0;
		
   		//printf("malloc buf_size %d\n", buf_size);
		//bh->b_data = (uint8_t *)malloc(io_size);
		bh->b_data =dst;
		//gettimeofday(&end, NULL);
   		//printf("11do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);
		//userfs_info("_fcache_block bh->b_data %lu\n",bh->b_data);
		read_size = remain_size < buf_size ? remain_size: buf_size;

		

		list_add_tail(&bh->b_io_list, &io_list);
		//printf("list_add_tail\n");
		userfs_info("bitmap_clear (pos >> g_block_size_shift) %d,  read_size %d\n", (pos >> g_block_size_shift),read_size>>g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
		io_to_be_done += buf_size>>g_block_size_shift;

		//gettimeofday(&end, NULL);
   		//printf("2do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);


		if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
			
			panic("read error\n");
		}
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
			goto do_global_search;
		}
		goto do_io_aligned;

	}else{
		off_MB_aligned=ALIGN_FLOOR(off, g_lblock_size_bytes);
		if(_off!=off_MB_aligned){
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			//printf("blocknode->size %d\n", blocknode->size);
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size & FFF00;
				//userfs_info("blocknode->size %d, buf_size %d\n", blocknode->size,buf_size);
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				bh->b_data = dst;

				list_add_tail(&bh->b_io_list, &io_list);

				read_size = remain_size < buf_size ? remain_size: buf_size;
			
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
				//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
				io_to_be_done += buf_size>>g_block_size_shift;

				if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
					userfs_info("read error bitmapweight %d\n",bitmapweight);
					panic("read error\n");
				}
				if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
					bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
					goto do_global_search;
				}
				goto do_io_aligned;
			}
		}
	}
	
read_from_device:
	userfs_info("read_from_device off %d\n",_off);
	bmap_req.start_offset = _off;
	bmap_req.blk_count = 
		find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos;
	bmap_req.dev = 0;
	bmap_req.block_no = 0;
	bmap_req.blk_count_found = 0;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	ret = bmap(ip, &bmap_req);

	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO) {
		if (bmap_req.blk_count_found != bmap_req.blk_count) {
			userfs_debug("inum %u - count not find block in any storage layer\n", 
					ip->inum);
		}
		goto do_io_aligned;
	}

	userfs_info("bmap dev %d, bmap_req.block_no %lu, bmap_req.blk_count_found %d\n",bmap_req.dev,bmap_req.block_no,bmap_req.blk_count_found);
	if (bmap_req.dev == g_root_dev) {
		//printf("g_root_dev\n");
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	}else {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
		bh->b_data = dst;
		bh->b_size = bmap_req.blk_count_found << g_block_size_shift;
		bh->b_offset = 0;

		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		
		list_add_tail(&bh->b_io_list, &io_list);
	}

	if (ret == -EAGAIN) {
		//printf("ret1\n");
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
		//printf("ret2\n");
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		io_to_be_done += bmap_req.blk_count_found;

		
		if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
			userfs_info("read error bitmapweight %d\n",bitmapweight);
			panic("read error\n");
		}
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
			goto do_global_search;
		}

	}

	
	userfs_assert(io_to_be_done >= (io_size >> g_block_size_shift));

do_io_aligned:
	

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();
	
	list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {
		userfs_info("bh->b_size %d\n",bh->b_size);

#ifndef CONCURRENT		
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
#else
		threadpool_add(readthdpool, bh_submit_read_sync_IO, (void *)bh);
#endif
	}

	userfs_info("read finish io_size %d\n",io_size);
	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
	//gettimeofday(&end, NULL);
	//userfs_info("end   sec %d, usec %d\n", end.tv_sec, end.tv_usec);
    //printf("do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);

	return io_size;
}

typedef struct{
    void * func_arg;
    uint64_t* accessflag;
    uint16_t accesspos;
}poolargument;

int do_align_read_cache(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_info("do_aligned_read off %d,io_size %d\n",off,io_size);
	//printf("dst %p\n", dst);
	struct timeval start, end;
	struct timespec ts_end,ts_end1,ts_end2;
	clock_gettime(CLOCK_REALTIME, &ts_end);

	int io_to_be_done = 0, ret, i;
	//uint8_t devid =1;
	uint64_t l=0;
	offset_t key,MB_key,pMB_key=1073741824UL;
	offset_t _off, pos;
	offset_t off_MB_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list;
	uint32_t read_size=0, buf_size=0, remain_size = io_size;
	uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
	struct cache_copy_list copy_list[bitmap_size];
	bmap_req_t bmap_req;

	char *readbuf;
	//return io_size;
	//userfs_info("bitmap_size is %d\n", bitmap_size);
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	//INIT_LIST_HEAD(&io_list_log);

	//userfs_assert(io_size % g_block_size_bytes == 0);

	clock_gettime(CLOCK_REALTIME, &ts_end);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	for (pos = 0, _off = off; pos < io_size; ) {
		_fcache_block = NULL;
		key = (_off >> g_block_size_shift);
		MB_key = (_off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	
		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		if (enable_perf_stats) {
			g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.l0_search_nr++;
		}
		//userfs_info("pos %d\n",pos);
		userfs_info("key %d, MB_key %d, pMB_key %d\n",key,MB_key,pMB_key );
		if(pMB_key!=MB_key){
			//printf("pMB_key!=MB_key\n");
			_fcache_block = fcache_find(ip, MB_key);
			//printf("_fcache_block %p\n", _fcache_block);
			off_MB_aligned = ALIGN_FLOOR(_off, g_lblock_size_bytes);
			pMB_key=MB_key;
			//printf("_fcache_block %p\n", _fcache_block);
			//userfs_info("off %d, _off %d, MB_key %d, off_MB_aligned %d\n",off,_off,MB_key,off_MB_aligned);
			if (_fcache_block && _fcache_block->size>=(_off - off_MB_aligned + g_block_size_bytes)) {
				userfs_info("1_fcache_block->size %d,_off %d\n", _fcache_block->size,_off);
				if (_fcache_block->is_data_cached) {
					buf_size = _fcache_block->size - (_off - off_MB_aligned);
					read_size = (io_size+off-_off) < buf_size ? (io_size+off-_off): buf_size;
					//userfs_info("bufsize %d, readsize %d\n", buf_size,read_size);
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
						//userfs_info("dst pos %d\n", pos +(i<<g_block_size_shift));
						//userfs_info("fcache_block off %d\n", _off - off_MB_aligned+(i<<g_block_size_shift));
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (_off - off_MB_aligned)+(i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}

					list_move(&_fcache_block->l, &g_fcache_head.lru_head);
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
					userfs_debug("read cache hit: offset %lu(0x%lx) size %u\n", 
								off, off, io_size);
				}
			}else{
				
				if(_fcache_block){
					userfs_info("1_fcache_block->size %d\n", _fcache_block->size);
				}
				continue;
			}
		}else if(MB_key!=key){
			//userfs_info("pos %d\n",pos);
			_fcache_block = fcache_find(ip,key);
			if(_fcache_block){
				//userfs_info("pos %d\n",pos);
				userfs_info("2_fcache_block->size %d\n", _fcache_block->size);
				if (_fcache_block->is_data_cached && _fcache_block->size >=g_block_size_bytes) {
					buf_size = _fcache_block->size&(~(((unsigned long)0x1<<g_block_size_shift) -1));
					//userfs_info("1_fcache_block->size %d, buf_size %d, remain_size %d\n",_fcache_block->size,buf_size,remain_size);
					read_size = remain_size < buf_size ? remain_size: buf_size;
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
						//printf("pos %d, pos+(i<<g_block_size_shift) %d\n",pos,pos +(i<<g_block_size_shift));
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}

					// move the fcache entry to head of LRU
					list_move(&_fcache_block->l, &g_fcache_head.lru_head);
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
				
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
				}else if(_fcache_block->size <g_block_size_bytes && _fcache_block->size< remain_size){
					pos += g_block_size_bytes;
					_off += g_block_size_bytes;
					continue;
				}
			
			}

		}
		userfs_info("_fcache_block %p, buf_size %d\n", _fcache_block,buf_size);
		if(_fcache_block){
			pos += buf_size;
			_off += buf_size;
			userfs_info("_fcache_block->size %d, remain_size %d, buf_size %d\n",_fcache_block->size,remain_size,buf_size);
			//userfs_info("buf_size %d\n",buf_size);
		}else{
			pos += g_block_size_bytes;
			_off += g_block_size_bytes;
		}
		//userfs_info("1 pos %d\n",pos);
	}

	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		for (i = 0 ; i < bitmap_size; i++) {
			//userfs_info("i %d\n", i);
			if (copy_list[i].dst_buffer != NULL) {
				memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
				if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
					panic("read request overruns the user buffer\n");
			}
		
		}
		return io_size;
	}
	
	clock_gettime(CLOCK_REALTIME, &ts_end1);
	//userfs_printf("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	//userfs_printf("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	userfs_info("do_global_search remain_size %d\n",remain_size);
	//userfs_printf("do_global_search remain_size %d\n",remain_size);
	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
do_global_search:
	userfs_info("do_global_search %d\n",remain_size);
	//userfs_printf("do_global_search %d\n",remain_size);
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	pos = bitmap_pos << g_block_size_shift;
	_off = off + pos;
	
	
	userfs_info("_off %d, pos %d, bitmap_pos %d\n", _off,pos,bitmap_pos);
	//userfs_printf("_off %d, pos %d, bitmap_pos %d\n", _off,pos,bitmap_pos);

	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);


	userfs_info("block_node is %p, remain_size %d, off %d\n", blocknode,remain_size,_off);
	//userfs_printf("block_node is %p, remain_size %d, off %d\n", blocknode,remain_size,_off);
	if(blocknode!=NULL){
	
		buf_size = blocknode->size & FFF00;  //4K
		//userfs_info("block_node is fileoff %d,addr %ld,size %d, buf_size %d\n", blocknode->fileoff, blocknode->addr,blocknode->size, buf_size);
		//userfs_printf("block_node is fileoff %d,addr %ld,size %d, buf_size %d\n", blocknode->fileoff, blocknode->addr,blocknode->size, buf_size);
		//gettimeofday(&end, NULL);
   		//printf("10do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr + ((_off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
		//bh->b_size=blocknode->size;
		//userfs_info("blocknode->addr %ld, ((off- blocknode->fileoff) %ld,((off- blocknode->fileoff)>>g_block_size_shift) %ld\n",blocknode->addr,(_off- blocknode->fileoff),((_off- blocknode->fileoff)>>g_block_size_shift));
		bh->b_size =buf_size;
		bh->b_offset = 0;
		
   		//userfs_printf("blocknode->devid %d\n", blocknode->devid);
		
   		

   		//printf("buf %lu\n", readbuf);
   		
   		//printf("bh->b_data %lu\n", bh->b_data);
		//bh->b_data =dst;
		//gettimeofday(&end, NULL);
   		//printf("11do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);
		//userfs_info("_fcache_block bh->b_data %lu\n",bh->b_data);
		//read_size = remain_size < buf_size ? remain_size: buf_size;

		//_fcache_block = add_to_read_cache(ip, _off, buf_size, bh->b_data);
		//read_size = remain_size < buf_size ? remain_size: buf_size;
		//userfs_info("read_size %d, l %d\n", read_size,l);

		//gettimeofday(&end, NULL);
   		//printf("12do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);
   		//clock_gettime(CLOCK_REALTIME, &ts_end);
		//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
		if(blocknode->devid!=g_root_dev){
   			ret = posix_memalign((void **)&readbuf, 4096, buf_size);
			if (ret != 0)
				err(1, "posix_memalign");
			bh->b_data = readbuf;
   			
			read_size = blocknode->size -(_off- blocknode->fileoff);
			_fcache_block = add_to_read_cache(ip, blocknode->fileoff, buf_size, bh->b_data);
			read_size = remain_size < read_size ? remain_size: read_size;
			//userfs_info("read_size %d,buf_size %d\n", read_size,buf_size);
			for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
				//printf("copy_list %d\n",l);
				//printf("copy_list[%d], pos +(i<<g_block_size_shift) %d\n",l,pos +(i<<g_block_size_shift));
				//userfs_info("pos %d, i %d\n", pos,i);
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data +(_off- blocknode->fileoff)+ (i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
			}
		}else{
			//_fcache_block = add_to_read_cache(ip, _off, buf_size, bh->b_data);
			bh->b_data = dst+pos;
			read_size = remain_size < buf_size ? remain_size: buf_size;
			//userfs_info("read_size %d, l %d\n", read_size,l);
		}
		list_add_tail(&bh->b_io_list, &io_list);
		//printf("list_add_tail\n");
		//userfs_info("bitmap_clear (pos >> g_block_size_shift) %d,  read_size %d\n", (pos >> g_block_size_shift),read_size>>g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
		io_to_be_done += buf_size>>g_block_size_shift;

		//gettimeofday(&end, NULL);
   		//printf("2do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);


		//clock_gettime(CLOCK_REALTIME, &ts_end);
		//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
				panic("read error\n");
			}
			bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
			goto do_global_search;
		}
		goto do_io_aligned;

	}else{
		off_MB_aligned=ALIGN_FLOOR(off, g_lblock_size_bytes);
		if(_off!=off_MB_aligned){
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			//printf("blocknode->size %d\n", blocknode->size);
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size & FFF00;
				//userfs_info("blocknode->size %d, buf_size %d\n", blocknode->size,buf_size);
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				
				if(blocknode->devid==1){
					bh->b_data = (uint8_t *)malloc(buf_size);
   				}else{
   					//char *readbuf;
   					ret = posix_memalign((void **)&readbuf, 4096, buf_size);
					if (ret != 0)
						err(1, "posix_memalign");
					bh->b_data =readbuf;
   				}
				//bh->b_data = malloc(buf_size);

				list_add_tail(&bh->b_io_list, &io_list);

				_fcache_block = add_to_read_cache(ip, off_MB_aligned, buf_size, bh->b_data);

				read_size = remain_size < buf_size ? remain_size: buf_size;
				if(blocknode->devid!=g_root_dev){
					for (i = 0; i < (read_size >>g_block_size_shift); i++,l++){
					//for (l = 0; l < (blocknode->size>>g_block_size_shift); l++){
						//userfs_info("copy_list read_size i %d\n",i);
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (off- blocknode->fileoff)+ (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}
				}
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
				//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
				io_to_be_done += buf_size>>g_block_size_shift;

				if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
					//userfs_printf("read error bitmapweight %d\n",bitmapweight);
					panic("read error\n");
				}
				if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
					bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
					goto do_global_search;
				}
				goto do_io_aligned;
			}
		}
	}
	
read_from_device:
	userfs_info("read_from_device off %d\n",_off);
	bmap_req.start_offset = _off;
	bmap_req.blk_count = 
		find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos;
	bmap_req.dev = 0;
	bmap_req.block_no = 0;
	bmap_req.blk_count_found = 0;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	ret = bmap(ip, &bmap_req);

	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO) {
		if (bmap_req.blk_count_found != bmap_req.blk_count) {
			userfs_debug("inum %u - count not find block in any storage layer\n", 
					ip->inum);
		}
		goto do_io_aligned;
	}

	//userfs_info("bmap dev %d, bmap_req.block_no %lu, bmap_req.blk_count_found %d\n",bmap_req.dev,bmap_req.block_no,bmap_req.blk_count_found);
	if (bmap_req.dev == g_root_dev) {
		//printf("g_root_dev\n");
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		//bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	}else {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
		//bh->b_data = dst;
		bh->b_size = bmap_req.blk_count_found << g_block_size_shift;
		//bh->b_data = malloc(bh->b_size);
		if(bmap_req.dev==1){
			bh->b_data = (uint8_t *)malloc(bh->b_size);
   		}else{
   			//char *readbuf;
   			ret = posix_memalign((void **)&readbuf, 4096, bh->b_size);
			if (ret != 0)
				err(1, "posix_memalign");
			bh->b_data=readbuf;
   		}
		bh->b_offset = 0;

		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		
		if(bmap_req.blk_count_found==512){
			_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);
			for (i = 0; i < (read_size >> g_block_size_shift);i++, l++){
				//printf("copy_list3\n");
			//		cur += g_lblock_size_bytes) {
				//printf("l %d\n", l);
				//userfs_info("pos %d, i %d, offset %d\n", pos, (i<<g_block_size_shift),(off-off_MB_aligned)+(i<<g_block_size_shift));
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data+(off-off_MB_aligned)+(i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
				//printf("copy_list[%d] %p\n", l,&copy_list[l]);
				//printf("copy_list[%d].dst_buffer %p, pos %d\n", l,copy_list[l].dst_buffer,pos);
			}
		}else if(bmap_req.blk_count_found==1){
			_fcache_block = add_to_read_cache(ip, _off, g_block_size_bytes, bh->b_data);
			copy_list[l].dst_buffer = dst + pos;
			copy_list[l].cached_data = _fcache_block->data;
			copy_list[l].size = g_block_size_bytes;

			//printf("copy_list[%d].dst_buffer %p, pos %d\n",l, copy_list[l].dst_buffer,pos);
				
			l++;
				
		}else{
			printf("unkonwn error\n");
		}

		list_add_tail(&bh->b_io_list, &io_list);
	}

	if (ret == -EAGAIN) {
		//printf("ret1\n");
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
		//printf("ret2\n");
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		io_to_be_done += bmap_req.blk_count_found;

		
		if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
			userfs_info("read error bitmapweight %d\n",bitmapweight);
			panic("read error\n");
		}
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			bitmapweight= bitmap_weight(io_bitmap, bitmap_size);
			goto do_global_search;
		}

	}

	
	userfs_assert(io_to_be_done >= (io_size >> g_block_size_shift));

do_io_aligned:
	

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();
	
	clock_gettime(CLOCK_REALTIME, &ts_end1);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {
		//userfs_info("bh->b_size %d\n",bh->b_size);
		bh_submit_read_sync_IO(bh);
		//bh_release(bh);	
		
	}
	
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	for (i = 0 ; i < bitmap_size; i++) {
		//printf("memmove %i\n",i);
		if (copy_list[i].dst_buffer != NULL) {
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
		
	}
	clock_gettime(CLOCK_REALTIME, &ts_end2);
	//clock_gettime(CLOCK_REALTIME, &ts_end);
	//userfs_printf("0readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	//userfs_printf("1readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
	//userfs_printf("2readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end2.tv_sec, ts_end2.tv_nsec);
	//userfs_info("read finish io_size %d\n",io_size);
/*	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
	*/
	//gettimeofday(&end, NULL);
	//userfs_info("end   sec %d, usec %d\n", end.tv_sec, end.tv_usec);
    //printf("do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);

	return io_size;
}

int do_align_read_cache1(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_printf("do_aligned_read off %d,io_size %d\n",off,io_size);
	//printf("dst %p\n", dst);
	struct timeval start, end;
	struct timespec ts_end,ts_end1,ts_end2,ts_end3,ts_end4,ts_end5;
	//clock_gettime(CLOCK_REALTIME, &ts_end);

	int io_to_be_done = 0, ret, i;
	//uint8_t devid =1;
	uint64_t l=0;
	offset_t key,MB_key;
	offset_t _off, pos;
	offset_t off_MB_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list;
	uint32_t read_size=0, buf_size=0, remain_size = io_size;
	uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
	struct cache_copy_list copy_list[bitmap_size];
	bmap_req_t bmap_req;

	char *readbuf;
	//return io_size;
	//userfs_info("bitmap_size is %d\n", bitmap_size);
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	//INIT_LIST_HEAD(&io_list_log);

	//userfs_assert(io_size % g_block_size_bytes == 0);

	//clock_gettime(CLOCK_REALTIME, &ts_end);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	_off = off;
	//for (pos = 0, _off = off; pos < io_size; ) {
	//_fcache_block = NULL;
	pos=0;
	key = (_off >> g_block_size_shift);
	MB_key = (_off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	
	//userfs_info("key %d, MB_key %d\n",key,MB_key );
	_fcache_block = fcache_find(ip, MB_key);
	userfs_printf("_fcache_block %p\n", _fcache_block);
	off_MB_aligned = ALIGN_FLOOR(_off, g_lblock_size_bytes);
	if (_fcache_block && _fcache_block->size>=(_off - off_MB_aligned + g_block_size_bytes)) {
		//userfs_printf("1_fcache_block->size %d,_off %d\n", _fcache_block->size,_off);
		buf_size = _fcache_block->size - (_off - off_MB_aligned);
		read_size = (io_size+off-_off) < buf_size ? (io_size+off-_off): buf_size;
		//userfs_info("bufsize %d, readsize %d\n", buf_size,read_size);
		for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
			//userfs_info("dst pos %d\n", pos +(i<<g_block_size_shift));
			//userfs_info("fcache_block off %d\n", _off - off_MB_aligned+(i<<g_block_size_shift));
			copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
			copy_list[l].cached_data = _fcache_block->data + (_off - off_MB_aligned)+(i<<g_block_size_shift);
			copy_list[l].size = g_block_size_bytes;
		}

		list_move(&_fcache_block->l, &g_fcache_head.lru_head);
		bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done+=_fcache_block->size >>g_block_size_shift;
		userfs_debug("read cache hit: offset %lu(0x%lx) size %u\n", off, off, io_size);
	}
	//userfs_printf("_fcache_block %p, buf_size %d,read_size %d\n", _fcache_block,buf_size, read_size);
	if(_fcache_block){
		pos += buf_size;
		_off += buf_size;
		//userfs_printf("_fcache_block->size %d, remain_size %d, buf_size %d\n",_fcache_block->size,remain_size,buf_size);
		//userfs_info("buf_size %d\n",buf_size);
	}
	
	//printf("bitmap_weight(io_bitmap, bitmap_size) %d\n", bitmap_weight(io_bitmap, bitmap_size));
	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		for (i = 0 ; i < bitmap_size; i++) {
			//userfs_info("i %d\n", i);
			if (copy_list[i].dst_buffer != NULL) {
				memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
				if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
					panic("read request overruns the user buffer\n");
			}
		
		}
		return io_size;
	}
	
	//clock_gettime(CLOCK_REALTIME, &ts_end);
	//userfs_printf("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	//userfs_printf("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	//userfs_info("do_global_search remain_size %d\n",remain_size);
	//userfs_printf("do_global_search remain_size %d\n",remain_size);
	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
	uint64_t bmweight=0;
do_global_search:
	//userfs_info("do_global_search %d\n",remain_size);
	//userfs_printf("do_global_search %d\n",remain_size);
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	pos = bitmap_pos << g_block_size_shift;
	_off = off + pos;
	
	
	//userfs_info("_off %d, pos %d, bitmap_pos %d\n", _off,pos,bitmap_pos);
	//userfs_printf("_off %d, pos %d, bitmap_pos %d\n", _off,pos,bitmap_pos);
	clock_gettime(CLOCK_REALTIME, &ts_end);
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	clock_gettime(CLOCK_REALTIME, &ts_end1);
	//userfs_printf("0readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	//userfs_printf("1readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
	//userfs_printf("block_node is %p, remain_size %d, off %d\n", blocknode,remain_size,_off);
	//userfs_printf("block_node is %p, remain_size %d, off %d\n", blocknode,remain_size,_off);
	if(blocknode!=NULL){
	
		buf_size = blocknode->size & FFF00;  //4K
		//userfs_info("block_node is fileoff %d,addr %ld,size %d, buf_size %d\n", blocknode->fileoff, blocknode->addr,blocknode->size, buf_size);
		//userfs_printf("block_node is fileoff %d,addr %ld,size %d, buf_size %d\n", blocknode->fileoff, blocknode->addr,blocknode->size, buf_size);
		//gettimeofday(&end, NULL);
   		//printf("10do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);

		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr + ((_off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
		//bh->b_size=blocknode->size;
		//userfs_info("blocknode->addr %ld, ((off- blocknode->fileoff) %ld,((off- blocknode->fileoff)>>g_block_size_shift) %ld\n",blocknode->addr,(_off- blocknode->fileoff),((_off- blocknode->fileoff)>>g_block_size_shift));
		//userfs_printf("buf_size %d\n",buf_size);
		bh->b_size =buf_size;
		bh->b_offset = 0;
		
   		//userfs_printf("blocknode->devid %d\n", blocknode->devid);
		
   		

   		//printf("buf %lu\n", readbuf);
   		
   		//printf("bh->b_data %lu\n", bh->b_data);
		//bh->b_data =dst;
		//gettimeofday(&end, NULL);
   		//printf("11do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);
		//userfs_info("_fcache_block bh->b_data %lu\n",bh->b_data);
		//read_size = remain_size < buf_size ? remain_size: buf_size;

		//_fcache_block = add_to_read_cache(ip, _off, buf_size, bh->b_data);
		//read_size = remain_size < buf_size ? remain_size: buf_size;
		//userfs_info("read_size %d, l %d\n", read_size,l);

		//gettimeofday(&end, NULL);
   		//printf("12do_aligned_read use time %d usec\n", end.tv_usec - start.tv_usec);
   		//clock_gettime(CLOCK_REALTIME, &ts_end);
		//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
		if(blocknode->devid!=g_root_dev){
   			ret = posix_memalign((void **)&readbuf, 4096, buf_size);
			if (ret != 0)
				err(1, "posix_memalign");
			bh->b_data = readbuf;
   			
			read_size = blocknode->size -(_off- blocknode->fileoff);
			_fcache_block = add_to_read_cache(ip, blocknode->fileoff, buf_size, bh->b_data);
			read_size = remain_size < read_size ? remain_size: read_size;
			//userfs_info("read_size %d,buf_size %d\n", read_size,buf_size);
			for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
				//printf("copy_list %d\n",l);
				//printf("copy_list[%d], pos +(i<<g_block_size_shift) %d\n",l,pos +(i<<g_block_size_shift));
				//userfs_info("pos %d, i %d\n", pos,i);
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data +(_off- blocknode->fileoff)+ (i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
			}
		}else{
			//_fcache_block = add_to_read_cache(ip, _off, buf_size, bh->b_data);
			bh->b_data = dst+pos;
			read_size = remain_size < buf_size ? remain_size: buf_size;
			bh->b_size = read_size;
			//userfs_printf("read_size %d\n",read_size);
			//userfs_info("read_size %d, l %d\n", read_size,l);
		}
		list_add_tail(&bh->b_io_list, &io_list);
		//printf("list_add_tail\n");
		//userfs_info("bitmap_clear (pos >> g_block_size_shift) %d,  read_size %d\n", (pos >> g_block_size_shift),read_size>>g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
		io_to_be_done += buf_size>>g_block_size_shift;

		//gettimeofday(&end, NULL);
   		//printf("2do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);


		clock_gettime(CLOCK_REALTIME, &ts_end2);
		userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
		bmweight=bitmap_weight(io_bitmap, bitmap_size);
		if (bmweight != 0) {
			if(bitmapweight == bmweight){
				panic("read error\n");
			}
			bitmapweight= bmweight;
			goto do_global_search;
		}
		goto do_io_aligned;

	}else{
		userfs_printf("else in do_align_read_cache1 _off %d \n",_off);
		off_MB_aligned=ALIGN_FLOOR(off, g_lblock_size_bytes);
		if(_off!=off_MB_aligned){
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			//printf("blocknode->size %d\n", blocknode->size);
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size & FFF00;
				//userfs_info("blocknode->size %d, buf_size %d\n", blocknode->size,buf_size);
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				
				if(blocknode->devid==1){
					bh->b_data = (uint8_t *)malloc(buf_size);
   				}else{
   					//char *readbuf;
   					ret = posix_memalign((void **)&readbuf, 4096, buf_size);
					if (ret != 0)
						err(1, "posix_memalign");
					bh->b_data =readbuf;
   				}
				//bh->b_data = malloc(buf_size);

				list_add_tail(&bh->b_io_list, &io_list);

				

				read_size = remain_size < buf_size ? remain_size: buf_size;
				if(blocknode->devid!=g_root_dev){
					_fcache_block = add_to_read_cache(ip, off_MB_aligned, buf_size, bh->b_data);
					for (i = 0; i < (read_size >>g_block_size_shift); i++,l++){
					//for (l = 0; l < (blocknode->size>>g_block_size_shift); l++){
						//userfs_info("copy_list read_size i %d\n",i);
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (off- blocknode->fileoff)+ (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}
				}
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
				//bitmap_clear(io_bitmap, bitmap_pos, blocknode->size>>g_block_size_shift);
				io_to_be_done += buf_size>>g_block_size_shift;
				bmweight = bitmap_weight(io_bitmap, bitmap_size);
				if(bitmapweight == bmweight){
					//userfs_printf("read error bitmapweight %d\n",bitmapweight);
					panic("read error\n");
				}
				if (bmweight != 0) {
					bitmapweight= bmweight;
					goto do_global_search;
				}
				goto do_io_aligned;
			}
		}
	}
	
read_from_device:
	userfs_printf("read_from_device off %d\n",_off);
	bmap_req.start_offset = _off;
	bmap_req.blk_count = 
		find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos;
	bmap_req.dev = 0;
	bmap_req.block_no = 0;
	bmap_req.blk_count_found = 0;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	ret = bmap(ip, &bmap_req);

	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO) {
		if (bmap_req.blk_count_found != bmap_req.blk_count) {
			userfs_debug("inum %u - count not find block in any storage layer\n", 
					ip->inum);
		}
		goto do_io_aligned;
	}

	userfs_info("bmap dev %d, bmap_req.block_no %lu, bmap_req.blk_count_found %d\n",bmap_req.dev,bmap_req.block_no,bmap_req.blk_count_found);
	if (bmap_req.dev == g_root_dev) {
		//printf("g_root_dev\n");
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		//bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	}else {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
		//bh->b_data = dst;
		bh->b_size = bmap_req.blk_count_found << g_block_size_shift;
		//bh->b_data = malloc(bh->b_size);
		if(bmap_req.dev==1){
			bh->b_data = (uint8_t *)malloc(bh->b_size);
   		}else{
   			//char *readbuf;
   			ret = posix_memalign((void **)&readbuf, 4096, bh->b_size);
			if (ret != 0)
				err(1, "posix_memalign");
			bh->b_data=readbuf;
   		}
		bh->b_offset = 0;

		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		
		if(bmap_req.blk_count_found==512){
			_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);
			for (i = 0; i < (read_size >> g_block_size_shift);i++, l++){
				//printf("copy_list3\n");
			//		cur += g_lblock_size_bytes) {
				//printf("l %d\n", l);
				//userfs_info("pos %d, i %d, offset %d\n", pos, (i<<g_block_size_shift),(off-off_MB_aligned)+(i<<g_block_size_shift));
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data+(off-off_MB_aligned)+(i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
				//printf("copy_list[%d] %p\n", l,&copy_list[l]);
				//printf("copy_list[%d].dst_buffer %p, pos %d\n", l,copy_list[l].dst_buffer,pos);
			}
		}else if(bmap_req.blk_count_found==1){
			_fcache_block = add_to_read_cache(ip, _off, g_block_size_bytes, bh->b_data);
			copy_list[l].dst_buffer = dst + pos;
			copy_list[l].cached_data = _fcache_block->data;
			copy_list[l].size = g_block_size_bytes;

			//printf("copy_list[%d].dst_buffer %p, pos %d\n",l, copy_list[l].dst_buffer,pos);
				
			l++;
				
		}else{
			printf("unkonwn error\n");
		}

		list_add_tail(&bh->b_io_list, &io_list);
	}

	if (ret == -EAGAIN) {
		//printf("ret1\n");
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
		//printf("ret2\n");
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		io_to_be_done += bmap_req.blk_count_found;

		bmweight = bitmap_weight(io_bitmap, bitmap_size);
		if(bitmapweight == bmweight){
			userfs_info("read error bitmapweight %d\n",bitmapweight);
			panic("read error\n");
		}
		if (bmweight != 0) {
			bitmapweight= bmweight;
			goto do_global_search;
		}

	}

	
	userfs_assert(io_to_be_done >= (io_size >> g_block_size_shift));

do_io_aligned:
	

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();
	
	clock_gettime(CLOCK_REALTIME, &ts_end3);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	uint32_t size;
	list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {
		//userfs_info("bh->b_size %d\n",bh->b_size);
		//bh_submit_read_sync_IO(bh);
		//bh_release(bh);

#ifndef CONCURRENT		
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
#else
		size = bh->b_size;
		//threadpool_add(readthdpool, bh_submit_read_sync_IO, (void *)bh);
		threadpool_add(readpool[bh->b_dev], bh_submit_read_sync_IO, (void *)bh);
		//printf("bh->b_size %d, size %d\n", bh->b_size, size);
		while(bh->b_size!=size+1){
			;//printf("bh->b_size %d, size %d\n", bh->b_size, size);
		}
	//printf("read finish\n");
#endif
	
	}
	clock_gettime(CLOCK_REALTIME, &ts_end4);
	//userfs_info("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	for (i = 0 ; i < bitmap_size; i++) {
		//printf("memmove %i\n",i);
		if (copy_list[i].dst_buffer != NULL) {
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
		
	}
	clock_gettime(CLOCK_REALTIME, &ts_end5);
	//clock_gettime(CLOCK_REALTIME, &ts_end);
	
	userfs_printf("0readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	userfs_printf("1readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
	userfs_printf("2readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end2.tv_sec, ts_end2.tv_nsec);
	userfs_printf("3readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end3.tv_sec, ts_end3.tv_nsec);
	userfs_printf("4readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end4.tv_sec, ts_end4.tv_nsec);
	userfs_printf("5readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end5.tv_sec, ts_end5.tv_nsec);
	
	//userfs_info("read finish io_size %d\n",io_size);
/*	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
	*/
	//gettimeofday(&end, NULL);
	//userfs_info("end   sec %d, usec %d\n", end.tv_sec, end.tv_usec);
    //printf("do_align_read use time %d usec\n", end.tv_usec - start.tv_usec);

	return io_size;
}


int readi(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_printf("offset %d, readi %lu\n",off,io_size);
	struct timespec ts_end, ts_end1,ts_end2, ts_end3, ts_end4;
	clock_gettime(CLOCK_REALTIME, &ts_end);
	
	int ret = 0;
	uint8_t *_dst;
	offset_t _off, offset_end, offset_aligned, offset_small = 0;
	offset_t size_aligned = 0, size_prepended = 0, size_appended = 0, size_small = 0;
	int io_done;

	userfs_assert(off < ip->size);

	if (off + io_size > ip->size)
		io_size = ip->size - off;

	//userfs_info("file size %d, read io_size %d\n", ip->size, io_size);
	_dst = dst;
	_off = off;

	offset_end = off + io_size;
	offset_aligned = ALIGN(off, g_block_size_bytes);
	//printf("offset_end %d, offset_aligned %d\n", offset_end,offset_aligned);
	// aligned read. 对齐读
	if ((offset_aligned == off) &&
		(offset_end == ALIGN(offset_end, g_block_size_bytes))) {
		size_aligned = io_size;
		//printf("aligned read %d\n", size_aligned);
	}else { //small write, 小于一个块
		if ((offset_aligned == off && io_size < g_block_size_bytes) ||
				(offset_end < offset_aligned)) { 
			offset_small = off - ALIGN_FLOOR(off, g_block_size_bytes);
			size_small = io_size;
		} else {
			//前面不对齐数据大小size_prepended
			if (off < offset_aligned) {
				size_prepended = offset_aligned - off;
			} else
				size_prepended = 0;

			//后面不对齐数据大小size_appended
			size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
			if (size_appended > 0) {
				size_appended = g_block_size_bytes - size_appended;
			}
			//中间对齐数据大小size_appended
			size_aligned = io_size - size_prepended - size_appended; 
		}
	}

	//userfs_info("size_small %d, size_prepended %d, size_aligned %d, size_appended %d\n", size_small, size_prepended, size_aligned,size_appended);
	//clock_gettime(CLOCK_REALTIME, &ts_end1);
	//userfs_printf("readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
	if (size_small) {

		io_done = do_unaligned_read(ip, _dst, _off, size_small);

		userfs_assert(size_small == io_done);

		_dst += size_small;
		_off += size_small;
		ret += size_small;
	}

	if (size_prepended) {
		//userfs_info("do_unaligned_read size_prepended %d\n",size_prepended);
		io_done = do_unaligned_read(ip, _dst, _off, size_prepended);

		userfs_assert(size_prepended == io_done);

		_dst += size_prepended;
		_off += size_prepended;
		ret += size_prepended;
	}
	//clock_gettime(CLOCK_REALTIME, &ts_end2);
	if (size_aligned) {
		//printf("size_aligned read\n");
		/*
		struct buffer_head * bh;
		bh = bh_get_sync_IO(1, _off >>g_block_size_shift, BH_NO_DATA_ALLOC);
		bh->b_size = size_aligned;
		bh->b_offset = 0;
		bh->b_data = _dst;
		io_done = bh_submit_read_sync_IO(bh);
		clock_gettime(CLOCK_REALTIME, &ts_end3);
		bh_release(bh);	
		//printf("io_done %d",io_done);
		//userfs_info("do_aligned_read size_aligned %d\n",size_aligned);

		clock_gettime(CLOCK_REALTIME, &ts_end4);
		*/
		//userfs_printf("0 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
		//userfs_printf("1 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
		//userfs_printf("2 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end2.tv_sec, ts_end2.tv_nsec);
		//userfs_printf("3 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end3.tv_sec, ts_end3.tv_nsec);
		//userfs_printf("4 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end4.tv_sec, ts_end4.tv_nsec);
		clock_gettime(CLOCK_REALTIME, &ts_end3);
		
		io_done = do_align_read_cache1(ip, _dst, _off, size_aligned);
		//userfs_assert(size_aligned == io_done);
	
		
		clock_gettime(CLOCK_REALTIME, &ts_end4);
		userfs_printf("3 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end3.tv_sec, ts_end3.tv_nsec);
		userfs_printf("4 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end4.tv_sec, ts_end4.tv_nsec);
		
		_dst += size_aligned;
		_off += size_aligned;
		ret += size_aligned;
		
	}

	if (size_appended) {
		//userfs_info("do_unaligned_read size_appended %d\n",size_appended);

		io_done = do_unaligned_read(ip, _dst, _off, size_appended);

		userfs_assert(size_appended == io_done);

		_dst += io_done;
		_off += io_done;
		ret += io_done;
	}
	//clock_gettime(CLOCK_REALTIME, &ts_end4);
		//userfs_printf("3 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end3.tv_sec, ts_end3.tv_nsec);
	//userfs_printf("4 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end4.tv_sec, ts_end4.tv_nsec);
	//printf("read ret %d\n", ret);
	//return io_size;
	return ret;
}
