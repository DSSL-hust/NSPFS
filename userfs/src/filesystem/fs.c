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
    
#ifdef CONCURRENT
   
    if(thpool_wait(writepool[1])&&thpool_wait(writepool[2])&&thpool_wait(writepool[3])&&thpool_wait(writepool[4]))
    	;
    //thpool_wait(writepool[1]);
    //thpool_wait(writepool[2]);
    //thpool_wait(writepool[3]);
    //thpool_wait(writepool[4]);
   
#endif
	if (!initialized)
		return ;

	fflush(stdout);
	fflush(stderr);


	

	enable_perf_stats = 0;

	//shutdown_log();

	enable_perf_stats = _enable_perf_stats;

	if (enable_perf_stats) 
		show_libfs_stats();


	append_addrinfo_log();

	ret=munmap(logstart,logspace);
	unsigned long endaddr = nblock_table[0][1]<<page_shift;
	munmap(mmapstart, endaddr); //destroy map memory
   	//close(pmemfd);   //close file

	if (ret == -1)
		panic("cannot unmap shared memory\n");
	//printf("logstart %lu, logbase %lu, logoffset %ld\n",logstart,logbase,logoffset );

	syscall(337,logname,logstart-logbase,logstart + logoffset-logbase);
	logstart=logbase+ logoffset;
	logsoff=logoffset;
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

	userfs_slab_pool_shared = (ncx_slab_pool_t *)(shm_base + 4096);
	
	userfs_slab_pool_shared->addr = shm_base + 4096;
	
	userfs_slab_pool_shared->addr = (shm_base + 4096) + _shm_slab_index * (SHM_SIZE / 2);
	
	userfs_slab_pool_shared->min_shift = 3;
	
	userfs_slab_pool_shared->end = userfs_slab_pool_shared->addr + (SHM_SIZE / 2);
	
	ncx_slab_init(userfs_slab_pool_shared);
}

static void shared_memory_init(void)
{
	int ret;
	
	shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
	
	if (shm_fd == -1) 
		panic("cannot open shared memory\n");

	
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
	if (shm_base == MAP_FAILED) 
		panic("cannot map shared memory\n");

	shm_slab_index = 0;
	shared_slab_init(shm_slab_index);
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
}

static void append_dir_inode_entry(struct inode *dir, struct fuckfs_dentry *entry){
	__le16 links_count;
	entry->entry_type = DIR_LOG;
	
	entry->file_type = 0;
	entry->invalid = 0;
	
	logaddr = logstart+logoffset;
	memcpy(logaddr, entry, sizeof(struct fuckfs_dentry));
	msync(logaddr,sizeof(struct fuckfs_dentry), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct fuckfs_dentry);
}
static void append_file_write_entry(struct inode *inode, struct fuckfs_extent *entry){

	entry->entry_type = FILE_WRITE;
	
	logaddr = logstart+logoffset;
	memcpy(logaddr, entry, sizeof(struct fuckfs_extent));
	msync(logaddr,sizeof(struct fuckfs_extent), PROT_READ | PROT_WRITE);
	logaddr = logaddr+sizeof(struct fuckfs_extent);
}

static void append_link_change_entry(struct inode *inode, struct link_change_entry *entry){

	entry->entry_type = LINK_CHANGE;
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
   
}


void sb_init(struct super_block *sb){
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_rwlock_init(&sb->wcache_rwlock, &rwlattr);
	
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
	printf("init_fs\n");
	system("echo 3 > /proc/sys/vm/drop_caches");
	int ret=0;
	int i;
	char buf[10]="fuckfs";
	const char *perf_profile;

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

#endif
	loginit(logname);
	device_init();

	mmapstart = g_bdev[1]->map_base_addr;

	entrynum=0;
#if 1
	if (!initialized) {

	
		struct inode *root_inode;
		
		sb = (struct super_block *)userfs_zalloc(sizeof(struct super_block));
		sb_init(sb);
		
		lru_options_t options;
		options.num_slot=4;
		options.node_size=6;
		inodelru = malloc_lru(&options);
		//INIT_LIST_HEAD(&(inodelru->list));
		//struct list_head *head = &(inodelru->list);
		
		debug_init();  

		cache_init();  
		shared_memory_init();
		
		locks_init();

		userfs_file_init();
		ADDRINFO = (struct fuckfs_addr_info *)malloc(sizeof(struct fuckfs_addr_info));
		uint64_t inode_base=syscall(335, ADDRINFO,30000);
#if 0
		ADDRINFO->ssd1_block_addr = ADDRINFO->ssd1_block_addr <<9;
		ADDRINFO->ssd2_block_addr = ADDRINFO->ssd2_block_addr <<9;
		ADDRINFO->ssd3_block_addr = ADDRINFO->ssd3_block_addr <<9;
#endif
		nvmstartblock=ADDRINFO->nvm_block_addr;
		
		for(i=0;i<inonum;i++){
   			fsinode_table[i]=inode_base+ 128*i;
   		}
		
		char rootname[256]="/mnt/pmem_emul";
		
		
		root_inode = iget(ROOTINO);
	 	
		if (!dlookup_find(rootname))
			dlookup_alloc_add(root_inode, rootname);

		initialized = 1;

		perf_profile = getenv("USERFS_PROFILE");

		if (perf_profile) 
			enable_perf_stats = 1;		
		else
			enable_perf_stats = 0;

		memset(&g_perf_stats, 0, sizeof(libfs_stat_t));

		clock_speed_mhz = get_cpu_clock_speed();

		//load_files(root_inode);


	}
#endif
	//printf("end init_fs\n");
}

void load_files(struct inode * ip){
	char *top;
	int rlen;
	
	__le64 blk_base = ip->root;
	char *block = (char *)malloc(g_block_size_bytes);

	memcpy(block, mmapstart+blk_base, g_block_size_bytes);

	struct fs_direntry *de = (struct fs_direntry *)block;
	top = block + g_block_size_bytes;
	while ((char *)de < top) {
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
	
	unsigned int data_bits=12;
	offset_t offset = 0;

	__le64 node[512];
	u64 bp = 0;
	unsigned long last_off;
	uint64_t flag =3;
	flag= ~(flag<<62);
	
	last_off = inode->size >> data_bits;

	if (inode->height == 0 && inode->root==0) {
		struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		ret=syscall(336, inode->inum, tinode);
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		inode->root=tinode->root;
		inode->height=tinode->height;
	}
	
	if(inode->height == 0){
		devid = inode->root>>62;
		//printf("inode devid %d\n", devid);
		if (devid>0){
			
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
			memcpy(node, mmapstart+blknode->addr, g_block_size_bytes);
			

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
	
	bh = bh_get_sync_IO(dev, 1, BH_NO_DATA_ALLOC);
	bh->b_size = g_block_size_bytes;
	bh->b_data = userfs_zalloc(g_block_size_bytes);
	//printf("read_superblock2\n");
	bh_submit_read_sync_IO(bh);
	userfs_io_wait(dev, 1);
	//printf("read_superblock3\n");
	if (!bh)
		panic("cannot read superblock\n");

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
   
    inode->flags=0;
    inode->flags |= I_VALID;
    inode->inum = inum;
    inode->nlink = tinode->i_nlink;
    inode->size = tinode->i_size;
    inode->ksize = tinode->i_size;
    inode->atime = tinode->i_atime;
    inode->mtime = tinode->i_mtime;
    inode->ctime = tinode->i_ctime;
  
    if(S_ISDIR(inode->i_mode))
    	inode->itype=T_DIR;
    else if(S_ISREG(inode->i_mode))
    	inode->itype=T_FILE;
    else
    	inode->itype=T_DEV;
    
    inode->blocks = tinode->i_blocks;
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
    inode->usednvms = ALIGN_FLOOR(inode->ssdoff,g_lblock_size_bytes);
   
    
    pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	inode->de_cache = NULL;
	//INIT_LIST_HEAD(&inode->i_slru_head);
	
    pthread_mutex_init(&inode->i_mutex, NULL);
    userfs_info("read_ondisk_inode ino %d\n",inum);
    return inode;
}

// Allocate "in-memory" inode. 
// on-disk inode is created by icreate
struct inode* ialloc(uint32_t inum)
{
	int ret;
	struct inode *ip;
	pthread_rwlockattr_t rwlattr;

	
	ip = icache_find(inum);
	if (!ip) 
		ip = icache_alloc_add(inum);

	
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
	ip->flags = 0;
	ip->flags |= I_VALID;
	ip->i_ref = 1;
	ip->n_de_cache_entry = 0;
	ip->i_dirty_dblock = RB_ROOT;
	ip->i_sb = sb;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);
	pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
	ip->fcache = NULL;
	ip->n_fcache_entries = 0;

#ifdef KLIB_HASH
	userfs_debug("allocate hash %u\n", ip->inum);
	ip->fcache_hash = kh_init(fcache);
#endif
	ip->de_cache = NULL;
	pthread_spin_init(&ip->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	
	INIT_LIST_HEAD(&ip->i_slru_head);
	
	pthread_mutex_init(&ip->i_mutex, NULL);

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
	struct inode *_inode;
	lru_node_t *l, *tmp;
	userfs_assert(inode->i_ref < 2);
	//
	if (inode->i_ref == 1 && 
			(inode->flags & I_VALID) && 
			inode->nlink == 0) {
		if (inode->flags & I_BUSY)
			panic("Inode must not be busy!");


		inode->flags &= ~I_BUSY;
	}
	//printf("inode\n");
	ilock(inode);
	if(inode->itype == T_FILE){
		RB_EMPTY_ROOT(&inode->blktree);
	}else if(inode->itype == T_DIR){
		art_tree_destroy(&inode->dentry_tree);
	}
	
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

	userfs_get_time(&ip->mtime);
	ip->atime = ip->mtime; 
	
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
struct inode* iget(uint32_t inum)
{
	//userfs_info("inode iget %d\n",inum);
	struct inode *ip;

	ip = icache_find(inum);
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
		pthread_rwlockattr_t rwlattr;
		ip=read_ondisk_inode(inum); 
		ip = icache_add(ip);
		ip->n_de_cache_entry = 0;
		ip->i_dirty_dblock = RB_ROOT;
		ip->i_sb = sb;
		ip->fcache = NULL;
		ip->n_fcache_entries = 0;
		ip->devid=0;

		ip->i_ref=1;
		
		pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

		pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
		
#ifdef KLIB_HASH
		ip->fcache_hash = kh_init(fcache);
#endif
		ip->blktree=RB_ROOT;
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
	pthread_mutex_lock(&ip->i_mutex);
	ip->flags |= I_BUSY;
}

void iunlock(struct inode *ip)
{
	pthread_mutex_unlock(&ip->i_mutex);
	ip->flags &= ~I_BUSY;
}

/* iput does not deallocate inode. it just drops reference count. 
 * An inode is explicitly deallocated by ideallc() 
 */
void iput(struct inode *ip)
{
	pthread_mutex_lock(&ip->i_mutex);

	userfs_muffled("iput num %u ref %u nlink %u\n", 
			ip->inum, ip->i_ref, ip->nlink);
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
	int ret = 0;
	uint8_t devid=1;
	unsigned int data_bits=12;
	offset_t offset = bmap_req->start_offset;

	__le64 node[512];
	u64 bp = 0;
	unsigned long blocknr;
	uint64_t flag =3;
	flag= ~(flag<<62);
	if (offset >= inode->ksize)
		return -ENXIO;
	if (inode->height == 0 && inode->root==0) {
		struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		userfs_info("fetch inode ino %d\n",inode->inum);
		ret=syscall(336, inode->inum, tinode);
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		inode->root=tinode->root;
		inode->height=tinode->height;
	}
	if(inode->height == 0){
		devid = inode->root>>62;
		if (devid>0){
			bmap_req->block_no =(inode->root & flag)>>g_block_size_shift;
			bmap_req->dev =devid+1;
			bmap_req->blk_count_found = 512;
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
			return 0;
		}else{
			memcpy(node, mmapstart+inode->root, g_block_size_bytes);
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
			
			return 0;
		}
	}
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
		bit_shift = height * META_BLK_SHIFT;
		idx = blocknr >> bit_shift;
		bp = le64_to_cpu(node[idx]);
		if (bp == 0)
			return 0;
		blocknr = blocknr & ((1 << bit_shift) - 1);
		height--;
	}

	devid = bp >>62;
	if (devid >0){
		//printf("devid %d\n", devid);
		bmap_req->block_no =(bp & flag)>>g_block_size_shift;
		bmap_req->dev =devid +1;
		bmap_req->blk_count_found = 512;
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
	struct fcache_block *_fcache_block;
	
	_fcache_block = fcache_find(inode, (off >> g_block_size_shift));
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
	list_move(&_fcache_block->l, &g_fcache_head.lru_head); 
	//printf("list_move\n");
	if (g_fcache_head.n > g_max_read_cache_blocks) {
		userfs_info("evict_read_cache size %d, g_max_read_cache_blocks %d\n",g_fcache_head.n, g_max_read_cache_blocks);
		evict_read_cache(inode, g_fcache_head.n - g_max_read_cache_blocks);
		//userfs_info("evict_read_cache size %d\n",g_fcache_head.n);
	}
	return _fcache_block;
}

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
	
	int io_done = 0, ret;
	uint8_t devid=1;
	offset_t key, off_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	unsigned long bfileoff,rfileoff,readsize;
	
	bmap_req_t bmap_req;

	userfs_assert(io_size < g_block_size_bytes);

	key = (off >> g_block_size_shift);
	offset_t MB_key = (off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	
	off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);

	offset_t off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	_fcache_block = fcache_find(ip, key);

	//userfs_info("_fcache_block %p\n", _fcache_block);
	if (enable_perf_stats) {
		g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.l0_search_nr++;
	}

	if (_fcache_block) {
		// read cache hit
		//printf("read cache hit\n");
		if (_fcache_block->is_data_cached && _fcache_block->size >=(off-off_aligned+io_size)) {
			memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			list_move(&_fcache_block->l, &g_fcache_head.lru_head);

			return io_size;
		} 
		else if (_fcache_block->log_addr) {
			addr_t block_no = _fcache_block->log_addr;

			userfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", 
					block_no, off, off, io_size);

			bh = bh_get_sync_IO(devid, block_no, BH_NO_DATA_ALLOC);

			bh->b_offset = off - off_aligned;
			bh->b_data = dst;
			bh->b_size = io_size;
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
			goto do_io_unaligned;
		}else{
			userfs_info("clear fcache_block %d, _fcache_block->size %d, size %d\n",key,_fcache_block->size, off-off_aligned+io_size);
		}
	}else if(MB_key!=key){
		_fcache_block = fcache_find(ip, MB_key);
		if (_fcache_block && _fcache_block->size>=(off - off_MB_aligned + io_size)) {
			if (_fcache_block->is_data_cached) {
				
				memmove(dst, _fcache_block->data + (off - off_MB_aligned), io_size);
				//printf("memmove\n");
				list_move(&_fcache_block->l, &g_fcache_head.lru_head);
				return io_size;
			} 
			else if (_fcache_block->log_addr) {
				addr_t block_no = _fcache_block->log_addr;
				userfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", 
						block_no, off, off, io_size);

				bh = bh_get_sync_IO(devid, block_no, BH_NO_DATA_ALLOC);

				bh->b_offset = off - off_MB_aligned;
				bh->b_data = dst;
				bh->b_size = io_size;

				bh_submit_read_sync_IO(bh);
				bh_release(bh);
				goto do_io_unaligned;
			}else{
				userfs_info("clear fcache_block %d\n",MB_key);
			}
		}
	}

	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),off_aligned);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	if(blocknode!=NULL){
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(bfileoff >= rfileoff){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr +(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = malloc(readsize);
			
			_fcache_block = add_to_read_cache(ip, off_aligned, readsize, bh->b_data);
			
			bh_submit_read_sync_IO(bh);
			
			bh_release(bh);
			
			memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			
			goto do_io_unaligned;
		}
	}else if(off_aligned!=off_MB_aligned){
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(blocknode!=NULL){
			userfs_info("1blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
		}
		if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			
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


	ret = bmap(ip, &bmap_req);
	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO)
		goto do_io_unaligned;

	bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
	bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

	
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
	else {
//		userfs_assert(_fcache_block == NULL);

		bh->b_data = malloc(g_lblock_size_bytes);
		bh->b_size = g_lblock_size_bytes;
		bh->b_offset = 0;

		_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);


		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		bh_submit_read_sync_IO(bh);


		if (enable_perf_stats) {
			g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.read_data_nr++;
		}

		bh_release(bh);

		memmove(dst, _fcache_block->data + (off - off_MB_aligned), io_size);
	}

do_io_unaligned:
	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

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

	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	INIT_LIST_HEAD(&io_list_log);

	userfs_assert(io_size % g_block_size_bytes == 0);

	for (pos = 0, _off = off; pos < io_size; ) {
		key = (_off >> g_block_size_shift);
		MB_key = (_off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	
		if (enable_perf_stats)
			start_tsc = asm_rdtscp();
		if (enable_perf_stats) {
			g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.l0_search_nr++;
		}

		if(pMB_key!=MB_key){
			_fcache_block = fcache_find(ip, MB_key);
			off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);
			pMB_key=MB_key;
			
			if (_fcache_block && _fcache_block->size>=(off - off_MB_aligned + g_block_size_bytes)) {
				
				if (_fcache_block->is_data_cached) {
					buf_size = _fcache_block->size;//&(~(((unsigned long)0x1<<g_block_size_shift) -1));
			
					read_size = remain_size < buf_size ? remain_size: buf_size;
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
					
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}

					
					list_move(&_fcache_block->l, &g_fcache_head.lru_head);
					
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
					
					userfs_debug("read cache hit: offset %lu(0x%lx) size %u\n", 
								off, off, io_size);
				} 
				
				else if (_fcache_block->log_addr) {
					addr_t block_no = _fcache_block->log_addr;
			
					userfs_debug("GET from update log: blockno %lx offset %lu(0x%lx) size %lu\n", 
							block_no, off, off, io_size);
					bh = bh_get_sync_IO(1, block_no, BH_NO_DATA_ALLOC);

					bh->b_offset = off - off_MB_aligned;
					bh->b_data = malloc(_fcache_block->size);
					bh->b_size = _fcache_block->size;

					list_add_tail(&bh->b_io_list, &io_list_log);
					read_size = remain_size < _fcache_block->size ? remain_size: _fcache_block->size;
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
					
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
				}
			}
		}else if(MB_key!=key){
			
			_fcache_block = fcache_find(ip,key);
			if(_fcache_block){
				
				
				if (_fcache_block->is_data_cached && _fcache_block->size >=g_block_size_bytes) {
					
					buf_size = _fcache_block->size&(~(((unsigned long)0x1<<g_block_size_shift) -1));
		
					read_size = remain_size < buf_size ? remain_size: buf_size;
					
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
						
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}

					// move the fcache entry to head of LRU
					list_move(&_fcache_block->l, &g_fcache_head.lru_head);
			
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;

		
				
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
			
				} 
				
				else if (_fcache_block->log_addr) {
					addr_t block_no = _fcache_block->log_addr;
					
					userfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", block_no, off, off, io_size);
					
					bh = bh_get_sync_IO(1, block_no, BH_NO_DATA_ALLOC);

					bh->b_offset = 0;
					bh->b_data = malloc(_fcache_block->size);
					bh->b_size = _fcache_block->size;

					list_add_tail(&bh->b_io_list, &io_list_log);
					read_size = remain_size < _fcache_block->size ? remain_size: _fcache_block->size;
					bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
					remain_size -= read_size;
	
				
					io_to_be_done+=_fcache_block->size >>g_block_size_shift;
				}

			}

		}
		if(_fcache_block){
			pos += buf_size;
			_off += buf_size;
			
		}else{
			pos += g_block_size_bytes;
			_off += g_block_size_bytes;
		}
		
	}

	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
		}
		
		for (i = 0 ; i < bitmap_size; i++) {
			
			if (copy_list[i].dst_buffer != NULL) {
				memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
				if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
					panic("read request overruns the user buffer\n");
			}
		
		}
		return io_size;
	}
	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
do_global_search:
	_off = off + (find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift);
	pos = find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift;
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	
	off_MB_aligned=ALIGN_FLOOR(off, g_lblock_size_bytes);

	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	if(blocknode!=NULL){
		
		if(blocknode->size<g_block_size_bytes && remain_size> blocknode->size){
			blocknode->size=g_block_size_bytes;
			goto read_from_device;
		}
		buf_size = blocknode->size &(~(((unsigned long)0x1<<g_block_size_shift) -1));

		bh->b_size =buf_size;
		bh->b_offset = 0;
   		
		bh->b_data = (uint8_t *)malloc(io_size);
		//bh->b_data =dst;
		
		_fcache_block = add_to_read_cache(ip, _off, buf_size, bh->b_data);
		read_size = remain_size < buf_size ? remain_size: buf_size;

		for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
			
			copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
			copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
			copy_list[l].size = g_block_size_bytes;
		}

		list_add_tail(&bh->b_io_list, &io_list);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		io_to_be_done += buf_size>>g_block_size_shift;

		
   		


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
	
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size &(~(((unsigned long)0x1<<g_block_size_shift) -1));
		
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				bh->b_data = malloc(buf_size);

				list_add_tail(&bh->b_io_list, &io_list);

				_fcache_block = add_to_read_cache(ip, off_MB_aligned, buf_size, bh->b_data);
				read_size = remain_size < buf_size ? remain_size: buf_size;
				for (i = 0; i < (read_size >>g_block_size_shift); i++,l++){
				
					copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
					copy_list[l].cached_data = _fcache_block->data + (i<<g_block_size_shift);
					copy_list[l].size = g_block_size_bytes;
				}
			
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
	
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

	// NVM case: no read caching.
	
	if (bmap_req.dev == g_root_dev) {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	} 
	// SSD and HDD cache: do read caching.
	else {
			bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
			bh->b_data = malloc(bmap_req.blk_count_found << g_block_size_shift);
			bh->b_size = bmap_req.blk_count_found << g_block_size_shift;
			bh->b_offset = 0;

			read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);

			if(bmap_req.blk_count_found==512){
				_fcache_block = add_to_read_cache(ip, off_MB_aligned, g_lblock_size_bytes, bh->b_data);
				for (i = 0; i < (read_size >> g_block_size_shift);i++, l++){
					
					copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
					copy_list[l].cached_data = _fcache_block->data+(i<<g_block_size_shift);
					copy_list[l].size = g_block_size_bytes;
				}
			}else if(bmap_req.blk_count_found==1){
				_fcache_block = add_to_read_cache(ip, _off, g_block_size_bytes, bh->b_data);
				copy_list[l].dst_buffer = dst + pos;
				copy_list[l].cached_data = _fcache_block->data;
				copy_list[l].size = g_block_size_bytes;
				
				l++;
				
			}else{
				printf("unkonwn error\n");
			}
		
		list_add_tail(&bh->b_io_list, &io_list);
	}
	
	if (ret == -EAGAIN) {
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {

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
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}

	
	for (i = 0 ; i < bitmap_size; i++) {
		if (copy_list[i].dst_buffer != NULL) {
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
		
	}

	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}
  

	return io_size;
}




int do_unalign_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_info("do_unaligned_read off %d, io_size %d\n",off, io_size);
	
	int io_done = 0, ret;
	offset_t key, off_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	unsigned long bfileoff,rfileoff,readsize;

	bmap_req_t bmap_req;


	userfs_assert(io_size < g_block_size_bytes);

	
	off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);
	offset_t off_MB_aligned = ALIGN_FLOOR(off, g_lblock_size_bytes);


	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	if (enable_perf_stats) {
		g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.l0_search_nr++;
	}


	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),off_aligned);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	if(blocknode!=NULL){
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(bfileoff >= rfileoff){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr +(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = dst;
			
			bh_submit_read_sync_IO(bh);
			
			bh_release(bh);
			
			goto do_io_unalign;
		}
	}else if(off_aligned!=off_MB_aligned){
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
	
		bfileoff = blocknode->fileoff + blocknode->size;
		rfileoff = off+io_size;
		if(blocknode!=NULL){
			userfs_info("1blocknode->size %d, fileoff %d, off %d\n", blocknode->size,blocknode->fileoff,off);
		}
		if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
			readsize=(bfileoff-off_aligned) >= g_block_size_bytes ? g_block_size_bytes: (rfileoff-off_aligned);
			
			bh=bh_get_sync_IO(blocknode->devid,(blocknode->addr+(off_aligned-blocknode->fileoff))>>g_block_size_shift,BH_NO_DATA_ALLOC);
			bh->b_size=readsize;
			bh->b_offset = 0;
			bh->b_data = dst;
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
			goto do_io_unalign;
		}
	}

	// global shared area search
	bmap_req.start_offset = off_aligned;
	bmap_req.blk_count_found = 0;
	bmap_req.blk_count = 1;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();


	ret = bmap(ip, &bmap_req);
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

		bh_submit_read_sync_IO(bh);
		bh_release(bh);


	} 
	// SSD and HDD cache: do read caching.
	else {


		bh->b_data = dst;
		bh->b_size = g_lblock_size_bytes;
		bh->b_offset = 0;
		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		bh_submit_read_sync_IO(bh);


		if (enable_perf_stats) {
			g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.read_data_nr++;
		}

		bh_release(bh);

	}

do_io_unalign:
	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Patch data from log (L0) if up-to-date blocks are in the update log.
	// This is required when partial updates are in the update log.

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
	struct timeval start, end;
	


	int io_to_be_done = 0, ret, i;
	offset_t _off, pos;
	offset_t off_MB_aligned;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list;
	uint32_t read_size, buf_size, remain_size = io_size;
	uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
	bmap_req_t bmap_req;
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	INIT_LIST_HEAD(&io_list);

	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
do_global_search:
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	pos = bitmap_pos << g_block_size_shift;
	_off = off + pos;

	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	if(blocknode!=NULL){
		buf_size = blocknode->size & FFF00;
		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr + ((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
		
		bh->b_size =buf_size;
		bh->b_offset = 0;
	
		bh->b_data =dst;

		read_size = remain_size < buf_size ? remain_size: buf_size;

		

		list_add_tail(&bh->b_io_list, &io_list);

		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += buf_size>>g_block_size_shift;

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
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size & FFF00;
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				bh->b_data = dst;

				list_add_tail(&bh->b_io_list, &io_list);

				read_size = remain_size < buf_size ? remain_size: buf_size;
			
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
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

	if (bmap_req.dev == g_root_dev) {
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
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
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

	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}

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

	int io_to_be_done = 0, ret, i;
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
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	
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
		if(pMB_key!=MB_key){
			_fcache_block = fcache_find(ip, MB_key);
			off_MB_aligned = ALIGN_FLOOR(_off, g_lblock_size_bytes);
			pMB_key=MB_key;
	
			if (_fcache_block && _fcache_block->size>=(_off - off_MB_aligned + g_block_size_bytes)) {
				if (_fcache_block->is_data_cached) {
					buf_size = _fcache_block->size - (_off - off_MB_aligned);
					read_size = (io_size+off-_off) < buf_size ? (io_size+off-_off): buf_size;
		
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
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
			_fcache_block = fcache_find(ip,key);
			if(_fcache_block){
				if (_fcache_block->is_data_cached && _fcache_block->size >=g_block_size_bytes) {
					buf_size = _fcache_block->size&(~(((unsigned long)0x1<<g_block_size_shift) -1));
					read_size = remain_size < buf_size ? remain_size: buf_size;
					for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
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
		
		if(_fcache_block){
			pos += buf_size;
			_off += buf_size;
			
		}else{
			pos += g_block_size_bytes;
			_off += g_block_size_bytes;
		}
	}

	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		for (i = 0 ; i < bitmap_size; i++) {
			
			if (copy_list[i].dst_buffer != NULL) {
				memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
				if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
					panic("read request overruns the user buffer\n");
			}
		
		}
		return io_size;
	}

	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
do_global_search:

	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	pos = bitmap_pos << g_block_size_shift;
	_off = off + pos;


	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	
	if(blocknode!=NULL){
	
		buf_size = blocknode->size & FFF00;  //4K


		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr + ((_off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
		
		bh->b_size =buf_size;
		bh->b_offset = 0;
		
 
		if(blocknode->devid!=g_root_dev){
   			ret = posix_memalign((void **)&readbuf, 4096, buf_size);
			if (ret != 0)
				err(1, "posix_memalign");
			bh->b_data = readbuf;
   			
			read_size = blocknode->size -(_off- blocknode->fileoff);
			_fcache_block = add_to_read_cache(ip, blocknode->fileoff, buf_size, bh->b_data);
			read_size = remain_size < read_size ? remain_size: read_size;
			
			for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
				
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data +(_off- blocknode->fileoff)+ (i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
			}
		}else{
	
			bh->b_data = dst+pos;
			read_size = remain_size < buf_size ? remain_size: buf_size;
			
		}
		list_add_tail(&bh->b_io_list, &io_list);
		
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		
		io_to_be_done += buf_size>>g_block_size_shift;

	
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
			
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size & FFF00;
				
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
				bh->b_size=buf_size;
				bh->b_offset = 0;
				
				if(blocknode->devid==1){
					bh->b_data = (uint8_t *)malloc(buf_size);
   				}else{
   				
   					ret = posix_memalign((void **)&readbuf, 4096, buf_size);
					if (ret != 0)
						err(1, "posix_memalign");
					bh->b_data =readbuf;
   				}
				

				list_add_tail(&bh->b_io_list, &io_list);

				_fcache_block = add_to_read_cache(ip, off_MB_aligned, buf_size, bh->b_data);

				read_size = remain_size < buf_size ? remain_size: buf_size;
				if(blocknode->devid!=g_root_dev){
					for (i = 0; i < (read_size >>g_block_size_shift); i++,l++){
					
						copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
						copy_list[l].cached_data = _fcache_block->data + (off- blocknode->fileoff)+ (i<<g_block_size_shift);
						copy_list[l].size = g_block_size_bytes;
					}
				}
				bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
				remain_size -= read_size;
				
				io_to_be_done += buf_size>>g_block_size_shift;

				if(bitmapweight == bitmap_weight(io_bitmap, bitmap_size)){
		
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

	if (bmap_req.dev == g_root_dev) {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;

		list_add_tail(&bh->b_io_list, &io_list);
	}else {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
		bh->b_size = bmap_req.blk_count_found << g_block_size_shift;

		if(bmap_req.dev==1){
			bh->b_data = (uint8_t *)malloc(bh->b_size);
   		}else{
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
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data+(off-off_MB_aligned)+(i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
			}
		}else if(bmap_req.blk_count_found==1){
			_fcache_block = add_to_read_cache(ip, _off, g_block_size_bytes, bh->b_data);
			copy_list[l].dst_buffer = dst + pos;
			copy_list[l].cached_data = _fcache_block->data;
			copy_list[l].size = g_block_size_bytes;
				
			l++;
				
		}else{
			printf("unkonwn error\n");
		}

		list_add_tail(&bh->b_io_list, &io_list);
	}

	if (ret == -EAGAIN) {
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
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
		bh_submit_read_sync_IO(bh);
		
	}
	
	for (i = 0 ; i < bitmap_size; i++) {
		if (copy_list[i].dst_buffer != NULL) {
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
		
	}
	return io_size;
}

int do_align_read_cache1(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_printf("do_aligned_read off %d,io_size %d\n",off,io_size);

	int io_to_be_done = 0, ret, i;
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
	
	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	
	_off = off;

	pos=0;
	key = (_off >> g_block_size_shift);
	MB_key = (_off >> g_lblock_size_shift)<<(g_lblock_size_shift-g_block_size_shift);
	
	_fcache_block = fcache_find(ip, MB_key);
	off_MB_aligned = ALIGN_FLOOR(_off, g_lblock_size_bytes);
	if (_fcache_block && _fcache_block->size>=(_off - off_MB_aligned + g_block_size_bytes)) {
		
		buf_size = _fcache_block->size - (_off - off_MB_aligned);
		read_size = (io_size+off-_off) < buf_size ? (io_size+off-_off): buf_size;
		for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
			copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
			copy_list[l].cached_data = _fcache_block->data + (_off - off_MB_aligned)+(i<<g_block_size_shift);
			copy_list[l].size = g_block_size_bytes;
		}

		list_move(&_fcache_block->l, &g_fcache_head.lru_head);
		bitmap_clear(io_bitmap, (pos >> g_block_size_shift), read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done+=_fcache_block->size >>g_block_size_shift;

	}
	if(_fcache_block){
		pos += buf_size;
		_off += buf_size;
	}
	
	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		for (i = 0 ; i < bitmap_size; i++) {
			if (copy_list[i].dst_buffer != NULL) {
				memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
				if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
					panic("read request overruns the user buffer\n");
			}
		
		}
		return io_size;
	}
	
	uint64_t bitmapweight=bitmap_weight(io_bitmap, bitmap_size);
	uint64_t bmweight=0;
do_global_search:
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	pos = bitmap_pos << g_block_size_shift;
	_off = off + pos;
	
	
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode=block_search(&(ip->blktree),_off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	if(blocknode!=NULL){
	
		buf_size = blocknode->size & FFF00;  //4K
		

		bh=bh_get_sync_IO(blocknode->devid,blocknode->addr + ((_off- blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
		
		bh->b_size =buf_size;
		bh->b_offset = 0;

		if(blocknode->devid!=g_root_dev){
   			ret = posix_memalign((void **)&readbuf, 4096, buf_size);
			if (ret != 0)
				err(1, "posix_memalign");
			bh->b_data = readbuf;
   			
			read_size = blocknode->size -(_off- blocknode->fileoff);
			_fcache_block = add_to_read_cache(ip, blocknode->fileoff, buf_size, bh->b_data);
			read_size = remain_size < read_size ? remain_size: read_size;
			
			for (i =0; i < (read_size >>g_block_size_shift); i++,l++){
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data +(_off- blocknode->fileoff)+ (i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
			}
		}else{
			bh->b_data = dst+pos;
			read_size = remain_size < buf_size ? remain_size: buf_size;
			bh->b_size = read_size;

		}
		list_add_tail(&bh->b_io_list, &io_list);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;

		io_to_be_done += buf_size>>g_block_size_shift;

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
		off_MB_aligned=ALIGN_FLOOR(off, g_lblock_size_bytes);
		if(_off!=off_MB_aligned){
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			struct block_node *blocknode=block_search(&(ip->blktree),off_MB_aligned);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			if(blocknode!=NULL && blocknode->size>=(off - off_MB_aligned + io_size)){
				buf_size = blocknode->size & FFF00;
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

	if (bmap_req.dev == g_root_dev) {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		//bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	}else {
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no , BH_NO_DATA_ALLOC);
		bh->b_size = bmap_req.blk_count_found << g_block_size_shift;
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
				copy_list[l].dst_buffer = dst + pos +(i<<g_block_size_shift);
				copy_list[l].cached_data = _fcache_block->data+(off-off_MB_aligned)+(i<<g_block_size_shift);
				copy_list[l].size = g_block_size_bytes;
			}
		}else if(bmap_req.blk_count_found==1){
			_fcache_block = add_to_read_cache(ip, _off, g_block_size_bytes, bh->b_data);
			copy_list[l].dst_buffer = dst + pos;
			copy_list[l].cached_data = _fcache_block->data;
			copy_list[l].size = g_block_size_bytes;
				
			l++;
				
		}else{
			printf("unkonwn error\n");
		}

		list_add_tail(&bh->b_io_list, &io_list);
	}

	if (ret == -EAGAIN) {
		read_size = remain_size < (bmap_req.blk_count_found<<g_block_size_shift) ? remain_size: (bmap_req.blk_count_found<<g_block_size_shift);
		bitmap_clear(io_bitmap, bitmap_pos, read_size >>g_block_size_shift);
		remain_size -= read_size;
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
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
	
	uint32_t size;
	list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {

#ifndef CONCURRENT		
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
#else
		size = bh->b_size;
		
		threadpool_add(readpool[bh->b_dev], bh_submit_read_sync_IO, (void *)bh);
		
		while(bh->b_size!=size+1){
			;
		}
	
#endif
	
	}
	for (i = 0 ; i < bitmap_size; i++) {
	
		if (copy_list[i].dst_buffer != NULL) {
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data, copy_list[i].size);
			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
		
	}


	return io_size;
}


int readi(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
	userfs_printf("offset %d, readi %lu\n",off,io_size);

	int ret = 0;
	uint8_t *_dst;
	offset_t _off, offset_end, offset_aligned, offset_small = 0;
	offset_t size_aligned = 0, size_prepended = 0, size_appended = 0, size_small = 0;
	int io_done;

	userfs_assert(off < ip->size);

	if (off + io_size > ip->size)
		io_size = ip->size - off;

	_dst = dst;
	_off = off;

	offset_end = off + io_size;
	offset_aligned = ALIGN(off, g_block_size_bytes);
	
	if ((offset_aligned == off) &&
		(offset_end == ALIGN(offset_end, g_block_size_bytes))) {
		size_aligned = io_size;
	}else { //small write, 小于一个块
		if ((offset_aligned == off && io_size < g_block_size_bytes) ||
				(offset_end < offset_aligned)) { 
			offset_small = off - ALIGN_FLOOR(off, g_block_size_bytes);
			size_small = io_size;
		} else {
			
			if (off < offset_aligned) {
				size_prepended = offset_aligned - off;
			} else
				size_prepended = 0;

			
			size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
			if (size_appended > 0) {
				size_appended = g_block_size_bytes - size_appended;
			}
			
			size_aligned = io_size - size_prepended - size_appended; 
		}
	}

	if (size_small) {

		io_done = do_unaligned_read(ip, _dst, _off, size_small);

		userfs_assert(size_small == io_done);

		_dst += size_small;
		_off += size_small;
		ret += size_small;
	}

	if (size_prepended) {
		io_done = do_unaligned_read(ip, _dst, _off, size_prepended);

		userfs_assert(size_prepended == io_done);

		_dst += size_prepended;
		_off += size_prepended;
		ret += size_prepended;
	}
	if (size_aligned) {
		
		
		io_done = do_align_read_cache1(ip, _dst, _off, size_aligned);
		//userfs_assert(size_aligned == io_done);
	
		_dst += size_aligned;
		_off += size_aligned;
		ret += size_aligned;
		
	}

	if (size_appended) {

		io_done = do_unaligned_read(ip, _dst, _off, size_appended);

		userfs_assert(size_appended == io_done);

		_dst += io_done;
		_off += io_done;
		ret += io_done;
	}
	
	return ret;
}
