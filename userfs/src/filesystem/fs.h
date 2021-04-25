#ifndef _FS_H_
#define _FS_H_

#include "global/global.h"
#include "global/types.h"
#include "global/defs.h"
//#include "log/log.h"
//#include "filesystem/stat.h"
//#include "filesystem/shared.h"
#include "global/mem.h"
#include "global/ncx_slab.h"
#include "stat.h"
#include "shared.h"
#include "io/buffer_head.h"
#include "io/block_io.h"
#include "io/device.h"
//#include "filesystem/extents.h"
//#include "filesystem/extents_bh.h"
#include "ds/uthash.h"
#include "ds/khash.h"
#include "filesystem/lru.h"
#include "threadpool/thpool.h"
#include <sys/mman.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline struct inode *icache_alloc_add(uint32_t inum);


// libuserfs Disk layout:
// [ boot block | sb block | inode blocks | free bitmap | data blocks | log blocks ]
// [ inode block | free bitmap | data blocks | log blocks ] is a block group.
// If data blocks is full, then file system will allocate a new block group.
// Block group expension is not implemented yet.

typedef unsigned short		umode_t;
typedef unsigned int	uid_t;
typedef unsigned int	gid_t;
//typedef long long 	loff_t;

typedef struct {
	uid_t val;
} kuid_t;


typedef struct {
	gid_t val;
} kgid_t;

struct tmpinode{
	umode_t			i_mode;
	unsigned short		i_opflags;
	kuid_t			i_uid;
	kgid_t			i_gid;
	unsigned int		i_flags;

	unsigned long i_ino;
	union {
		unsigned int i_nlink;
		unsigned int __i_nlink;
	};
	u8      height;         /* height of data b-tree; max 3 for now */
    u8      i_blk_type;     /* data block size this inode uses */
    __le64  root;               /* btree root. must be below qw w/ height */

	loff_t			i_size;
	struct timeval		i_atime;
	struct timeval		i_mtime;
	struct timeval		i_ctime;
	unsigned short          i_bytes;
	unsigned int		i_blkbits;
	blkcnt_t		i_blocks;
	unsigned long		i_state;
	uint32_t nvmblock;
	uint64_t blkoff;
	//unsigned long		dirtied_when;	/* jiffies of first dirtying */
	//unsigned long		dirtied_time_when;
};


// directory entry cache
struct dirent_data {
	userfs_hash_t hh;
	char name[DIRSIZ]; // key
	struct inode *inode;
	//offset_t offset;
};


/* A bug note. UThash has a weird bug that
 * if offset is uint64_t type, it cannot find data
 * It is OK to use 32 bit because the offset does not 
 * overflow 32 bit */
typedef struct dcache_key {
	uint32_t inum;
	uint32_t offset; // offset of directory inode / 4096.
} dcache_key_t;

// dirent array block (4KB) cache
struct dirent_block {
	dcache_key_t key; 
	userfs_hash_t hash_handle;
	uint8_t dirent_array[g_block_size_bytes];
	addr_t log_addr;
	uint32_t log_version;
};

typedef struct fcache_key {
	offset_t offset;
} fcache_key_t;

struct fcache_block {
	offset_t key;
	userfs_hash_t hash_handle;
	uint32_t size;
	uint32_t inum;
	addr_t log_addr;		// block # of update log
	uint8_t invalidate;
	uint32_t log_version;
	uint8_t is_data_cached;
	uint8_t *data;
	struct list_head l;	// entry for global list
};

struct cache_copy_list {
	uint8_t *dst_buffer;
	uint8_t *cached_data;
	uint32_t size;
	struct list_head l;
};

struct dlookup_data {
	userfs_hash_t hh;
	char path[MAX_PATH];	// key: canonical path
	struct inode *inode;
};

typedef struct bmap_request {
	// input 
	offset_t start_offset;
	uint32_t blk_count;
	// output
	addr_t block_no;
	uint32_t blk_count_found;
	uint8_t dev;
} bmap_req_t;

// statistics
typedef struct userfs_libfs_stats {
	uint64_t digest_wait_tsc;
	uint32_t digest_wait_nr;
	uint64_t l0_search_tsc;
	uint32_t l0_search_nr;
	uint64_t tree_search_tsc;
	uint32_t tree_search_nr;
	uint64_t log_write_tsc;
	uint64_t loghdr_write_tsc;
	uint32_t log_write_nr;
	uint64_t log_commit_tsc;
	uint32_t log_commit_nr;
	uint64_t read_data_tsc;
	uint32_t read_data_nr;
	uint64_t dir_search_tsc;
	uint32_t dir_search_nr_hit;
	uint32_t dir_search_nr_miss;
	uint32_t dir_search_nr_notfound;
	uint64_t ialloc_tsc;
	uint32_t ialloc_nr;
	uint64_t tmp_tsc;
	uint64_t bcache_search_tsc;
	uint32_t bcache_search_nr;
} libfs_stat_t;



extern struct lru g_fcache_head;

extern libfs_stat_t g_perf_stats;
extern uint8_t enable_perf_stats;

extern pthread_rwlock_t *icache_rwlock;
extern pthread_rwlock_t *dcache_rwlock;
extern pthread_rwlock_t *dlookup_rwlock;
extern pthread_rwlock_t *invalidate_rwlock;
extern pthread_rwlock_t *g_fcache_rwlock;
extern pthread_mutex_t *bitmap_lock;
extern pthread_mutex_t *nblock_lock;
extern pthread_mutex_t *sblock_lock;
extern pthread_mutex_t *applog_lock;
extern pthread_mutex_t *fileno_lock;
extern pthread_mutex_t *allossd_lock;
extern pthread_mutex_t *allossd1_lock;
extern pthread_mutex_t *allossd2_lock;
extern pthread_mutex_t *allossd3_lock;
extern pthread_mutex_t *allonvm_lock;

extern struct dirent_block *dirent_hash;
extern struct inode *inode_hash;
extern struct dlookup_data *dlookup_hash;

extern struct threadpool_t *writethdpool;
extern struct threadpool_t *readthdpool;
extern struct threadpool_t *committhdpool;

extern uint8_t nvmnum;
extern uint8_t ssdnum;

extern threadpool *writepool[5];
extern threadpool *readpool[5];

extern uint8_t writethdnum;
extern uint8_t readthdnum;
extern uint8_t committhdnum;


#define FUCKFS_DIR_PAD            4
#define FUCKFS_DIR_ROUND          (FUCKFS_DIR_PAD - 1)
#define FUCKFS_DIR_REC_LEN(name_len)  (((name_len) + 12 + FUCKFS_DIR_ROUND) & \
				      ~FUCKFS_DIR_ROUND)

#define inonum 30000 
#define FILENUM 65536
#define NVMSIZE 262144
#define nrangenum 100

#define SSDSIZE 67108864
#define srangenum 1000

#define page_shift 12
#define inode_shift 7
#define NVMCAP 1024*1024*1024*8 //8GB
#define NVMBSIZE 4096 //4KB
#define NVMBLOCK NVMCAP/NVMBSIZE

#define S_IFMT  0170000 /* type of file ，文件类型掩码*/
#define S_IFREG 0100000 /* regular 普通文件*/
#define S_IFDIR 0040000 /* directory 目录文件*/




#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

struct inodenode{
	struct list_head list;
	__le32 ino;
	//unsigned int  nvmsize;
	//uint32_t totalsize;
};

struct Fileno{
	struct list_head list;
	uint32_t fno;
};

struct Fileno filenos;

struct inode_lru_t *inodelru;

uint32_t inodenum;
char *mmapstart;
//int pmemfd;

static int entrynum;

//unsigned long long ssd1writespace, ssd2writespace, ssd3writespace; 
unsigned long long fsinode_table[inonum];
unsigned long long nblock_table[nrangenum][2];
unsigned long long sblock_table[srangenum][2];
char *logstart, *logend, *logaddr, *logbase;
extern uint8_t allodev;

#define logspace 8*1024*1024  //4M space for inode
//#define logspace 4*1024  //4M space for test
unsigned long long nvmstartblock;
unsigned long long logoffset,logsoff;

struct setattr_logentry {
    __le32  ino;
    u8  entry_type;
    u8  attr;
    __le16  mode;
    __le32  mtime;  //64th bit is mtime
    __le16  isdel;
	__le16  padding;
    __le32  ctime;
    __le32  uid;
    __le32  gid; 
    __le32  size;
} __attribute((__packed__));

struct fuckfs_extent {
    __le32  ino;
    u8  entry_type;
    u8 device_id;
    u8  blkoff_hi; 
    u8 isappend;
    __le32  mtime;
    uint32_t ext_block;    /* first logical block extent covers */
    uint16_t ext_len;      /* number of blocks covered by extent */
    uint16_t ext_start_hi; /* high 16 bits of physical block */
    uint32_t ext_start_lo; /* low 32 bits of physical block */
    __le32  size;
    __le32  blkoff_lo;
} __attribute((__packed__));


#define Addden 1
#define Delden 2
#define Ondisk 0
#define NAME_LEN 31

struct fuckfs_dentry {
    __le32  ino;
    u8  entry_type;
    u8  name_len;               /* length of the dentry name */
    u8  file_type;              /* file type */
    u8  addordel;
    __le32  mtime;          /* For both mtime and ctime */
    u8  invalid;        /* Invalid now? */
    u8 padding;
    //__le16    de_len;                 /* length of this dentry */
    __le16  links_count;
    __le64  fino;                    /* inode no pointed to by this entry */
    __le64  size;
    char    name[NAME_LEN + 1]; /* File name */
} __attribute((__packed__));



/* Do we need this to be 32 bytes? */
struct link_change_entry {
    __le32  ino;
    u8  entry_type;
    u8  padding;
    __le16  links;
    __le32  mtime;
    __le32  ctime;
    __le32  flags;
    __le32  generation;
    __le32  paddings[2];
} __attribute((__packed__));

//如何指定他们属于哪个文件



enum nova_entry_type {
	FILE_WRITE = 1,
	DIR_LOG,
	SET_ATTR,
	LINK_CHANGE,
};

struct fuckfs_addr_info {
	__le64 nvm_block_addr;
	__le64 ssd1_block_addr;
	__le64 ssd2_block_addr;
	__le64 ssd3_block_addr;
	__le64 free_inode_base;
}__attribute((__packed__));

struct fuckfs_addr_info *ADDRINFO;

#define META_BLK_SHIFT 9


extern int bitmapnum;

static uint32_t find_inode_bitmap(){
	//printf("find_inode_bitmap function %lu, %lu, %lu\n",logaddr,logstart,logend);
	int i;
	uint32_t ino;
	//userfs_info("inode bitmapnum is %d, inonum %d\n", bitmapnum,inonum);
	pthread_mutex_lock(bitmap_lock);
	for(i =bitmapnum;i<inonum;i++){
		//userfs_info("fsinode_table[%d] %d\n",i,fsinode_table[i]);
		if(fsinode_table[i]!=0){
			ino= fsinode_table[i];
			fsinode_table[i]=0;
			//ino=ino>>inode_shift;
			//userfs_info("inode ino is %d\n", ino);
			bitmapnum=i+1;
			//userfs_info("1inode bitmapnum is %d\n", bitmapnum);
			pthread_mutex_unlock(bitmap_lock);
			//printf("find_inode_bitmap ino %d\n",ino);
			return ino;
		}
	}
	pthread_mutex_unlock(bitmap_lock);
	return 0;
}

static uint32_t put_inode_bitmap(uint32_t ino){
	//userfs_info("put_inode_bitmap function %d\n",ino);
	int i;
	//uint32_t ino;
	pthread_mutex_lock(bitmap_lock);
	//userfs_info("00inode bitmapnum is %d\n", bitmapnum);
	for(i =bitmapnum-1;i>=0;i--){
		//userfs_info("fsinode_table[%d] %d\n",i,fsinode_table[i]);
		if(fsinode_table[i]==0){
			//ino= fsinode_table[i];
			fsinode_table[i]=ino;
			//ino=ino>>inode_shift;
			bitmapnum=i;
			//userfs_info("11inode bitmapnum is %d\n", bitmapnum);
			pthread_mutex_unlock(bitmap_lock);
			return ino;
		}
	}
	pthread_mutex_unlock(bitmap_lock);
	return 0;
}


//#define NVMSIZE 6*1024*1024
#define NVMSIZE 10*1024*1024 //40GB
static uint64_t allocate_nvmblock(uint64_t blocks){
	//userfs_info("allocate_nvmblock %d\n",blocks);
	uint64_t alloc_nspace;
	//userfs_info("ADDRINFO->nvm_block_addr %d\n", ADDRINFO->nvm_block_addr);
	pthread_mutex_lock(allonvm_lock);
	if(ADDRINFO->nvm_block_addr+blocks<=NVMSIZE){
		alloc_nspace = ADDRINFO->nvm_block_addr <<g_block_size_shift; //4K 地址-->字節地址
		//size =blocks;
		ADDRINFO->nvm_block_addr+=blocks;
		
	}else if(ADDRINFO->nvm_block_addr+blocks>NVMSIZE){
		//userfs_printf("ADDRINFO->nvm_block_addr %d\n", ADDRINFO->nvm_block_addr);
		ADDRINFO->nvm_block_addr = nvmstartblock;
		alloc_nspace = ADDRINFO->nvm_block_addr <<g_block_size_shift; //4K 地址-->字節地址
		//size =blocks;
		ADDRINFO->nvm_block_addr+=blocks;

		// = ADDRINFO->nvm_block_addr<<g_block_size_shift; //字節地址
		//size =NVMSIZE-ADDRINFO->nvm_block_addr;
		//ADDRINFO->nvm_block_addr+=size;
		
	}
	//userfs_info("blocks %d, alloc_nspace %ld\n",blocks,alloc_nspace);
	pthread_mutex_unlock(allonvm_lock);
	//printf("ADDRINFO->nvm_block_addr %ld\n", ADDRINFO->nvm_block_addr);
	return alloc_nspace;
}

static uint64_t allocate_ssdblocks(uint64_t blocks, uint8_t flag){
	uint64_t allospace;
	switch(flag){
		case 0:
			pthread_mutex_lock(allossd1_lock);
			allospace=ADDRINFO->ssd1_block_addr<<12; //4k block address  --> byte address
			ADDRINFO->ssd1_block_addr+=blocks;  //2M地址
			pthread_mutex_unlock(allossd1_lock);
			break;
		case 1:
			pthread_mutex_lock(allossd2_lock);
			allospace=ADDRINFO->ssd2_block_addr<<12; //4k block address  --> byte address
			ADDRINFO->ssd2_block_addr+=blocks;  //2M地址
			pthread_mutex_unlock(allossd2_lock);
			break;
		case 2:
			pthread_mutex_lock(allossd3_lock);
			allospace=ADDRINFO->ssd3_block_addr<<12; //4k block address  --> byte address
			ADDRINFO->ssd3_block_addr+=blocks;  //2M地址
			pthread_mutex_unlock(allossd3_lock);
			break;
		default:
			panic("allocate_ssdblock error\n");	

	}
	return allospace;
}
static uint64_t allocate_ssd1block(){
	//printf("allocate_ssdblock1\n");
	//userfs_info("ADDRINFO->ssd1_block_addr %ld, ADDRINFO->ssd2_block_addr %ld, ADDRINFO->ssd3_block_addr %ld\n", ADDRINFO->ssd1_block_addr,ADDRINFO->ssd2_block_addr,ADDRINFO->ssd3_block_addr);
	uint64_t allospace;
	pthread_mutex_lock(allossd1_lock);
	allospace=ADDRINFO->ssd1_block_addr<<21; //2M block address  --> byte address
	ADDRINFO->ssd1_block_addr+=1;  //2M地址
	pthread_mutex_unlock(allossd1_lock);
	return allospace;
}

static uint64_t allocate_ssd2block(){
	//printf("allocate_ssdblock2\n");
	//userfs_info("ADDRINFO->ssd1_block_addr %ld, ADDRINFO->ssd2_block_addr %ld, ADDRINFO->ssd3_block_addr %ld\n", ADDRINFO->ssd1_block_addr,ADDRINFO->ssd2_block_addr,ADDRINFO->ssd3_block_addr);
	uint64_t allospace;
	pthread_mutex_lock(allossd2_lock);
	allospace=ADDRINFO->ssd2_block_addr<<21; //2M block address  --> byte address
	ADDRINFO->ssd2_block_addr+=1;  //2M地址
	pthread_mutex_unlock(allossd2_lock);
	return allospace;
}

static uint64_t allocate_ssd3block(){
	//printf("allocate_ssdblock3\n");
	//userfs_info("ADDRINFO->ssd1_block_addr %ld, ADDRINFO->ssd2_block_addr %ld, ADDRINFO->ssd3_block_addr %ld\n", ADDRINFO->ssd1_block_addr,ADDRINFO->ssd2_block_addr,ADDRINFO->ssd3_block_addr);
	uint64_t allospace;
	pthread_mutex_lock(allossd3_lock);
	allospace=ADDRINFO->ssd3_block_addr<<21; //2M block address  --> byte address
	ADDRINFO->ssd3_block_addr+=1;  //2M地址
	pthread_mutex_unlock(allossd3_lock);
	return allospace;
}


static int lru_upsert(uint32_t ino, char* filename){
	//printf("lru_upsert-------------------------------------------\n");
	int ret=0;
	ret=insert_lru_node(inodelru,filename,strlen(filename),ino);
	//printf("ret is %d\n", ret);
	if(ret<0){
		printf("error insert to lru\n");
	}
	return ret;
/*
	struct list_head* head = &(inodelru->lru_nodes);
	struct list_head* p = NULL;
	printf("lru_list-------------------------------------------\n");
	list_for_each(p,head){
		lru_node_t* node = list_entry(p, lru_node_t, lru_list);
		printf("key is %s %d\n",node->key,node->ino);
	}
	printf("lru_list-------------------------------------------\n");

	size_t idx = inodelru->options.num_slot;
	printf("+++++++++++++++++++++++\n");
	for(int i=0;i<idx;i++){
		
		printf("i is %d\n",i);
		
		head = inodelru->hash_slots + i;
		p = NULL;
		list_for_each(p, head){
			lru_node_t* node = list_entry(p, lru_node_t, hash_list);
			printf("key is %s\n",node->key);
			
		}
	}
	printf("+++++++++++++++++++++++\n");
*/
}


static void devicecommit(){
	printf("devicecommit\n");
	int deviceid[committhdnum];
	for(int i=0;i<committhdnum;i++){
		deviceid[i]=i+1;
		threadpool_add(committhdpool, device_commit, (void *)deviceid[i]);
	}
} 

static void append_addrinfo_log(){
	//printf("append_addrinfo_log %d\n",logoffset);
	uint64_t flag=4095;
	uint64_t offset = logoffset;
	if ((logoffset&flag)>(4096-sizeof(struct fuckfs_addr_info))){
		offset = (logoffset >>page_shift +1)<<page_shift;
	}
	//printf("offset %d\n", offset);

	//printf("sizeof %d\n", sizeof(uint64_t));
	pthread_mutex_lock(applog_lock);
	logaddr = logstart + offset;
	//printf("memcpy0 %d\n",ADDRINFO->nvm_block_addr );
	//printf("memcpy1 %d\n",ADDRINFO->ssd1_block_addr);
	//printf("memcpy2 %d\n",ADDRINFO->ssd2_block_addr);
	//printf("memcpy3 %d\n",ADDRINFO->ssd3_block_addr);
	ADDRINFO->free_inode_base = fsinode_table[bitmapnum];
	//printf("memcpy4 %d\n", ADDRINFO->free_inode_base);
#if 0
	ADDRINFO->ssd1_block_addr=(ADDRINFO->ssd1_block_addr+511)>>9;
	ADDRINFO->ssd2_block_addr=(ADDRINFO->ssd2_block_addr+511)>>9;
	ADDRINFO->ssd3_block_addr=(ADDRINFO->ssd3_block_addr+511)>>9;
#endif	
	memcpy(logaddr, ADDRINFO, sizeof(struct fuckfs_addr_info));
	msync(logaddr,sizeof(struct fuckfs_addr_info), MS_SYNC);
	/*offset+=64;
	logaddr = logstart + offset;
	printf("memcpy2 %d\n",ssd2_block_addr);
	memcpy(logaddr, &ssd2_block_addr, 64);
	offset+=64;
	logaddr = logstart + offset;
	printf("memcpy3 %d\n",ssd3_block_addr);
	memcpy(logaddr, &ssd3_block_addr, 64);
	*/
	pthread_mutex_unlock(applog_lock);
	//printf("append_addr_info_log end %d\n", sizeof(uint64_t));
}

static void submit_log(){
	printf("submit_log logstart %ld, logbase %ld, logoffset %ld\n",logstart,logbase,logoffset);
	//syscall(337,logname,logstart-logbase,logstart + logoffset-logbase);
}
static void append_entry_log(struct fuckfs_extent *entry, uint8_t flag){
	//printf("append_entry_log function flag %d\n",flag);
	
	pthread_mutex_lock(applog_lock);
	//printf("append_entry_log function logoffset %d, total entry num %d\n",logoffset,entrynum++);
	if(logoffset+sizeof(struct fuckfs_dentry) > logspace){
		userfs_info("logoffset %d\n",logoffset);
		submit_log();
		logoffset=0;
	}
	logaddr = logstart + logoffset;
	//printf("entry ino is %d\n", entry->ino);
	//printf("append_entry_log function %lu, %lu, %lu\n",logaddr,logstart,logend);
	if(flag==0){
		//printf("append_entry_log memcpy1\n");
		memcpy(logaddr, entry, sizeof(struct fuckfs_extent));
		//printf("fuckfs_extent->entry_type %d\n",entry->entry_type);
		msync(logaddr,sizeof(struct fuckfs_extent), MS_SYNC);
		//printf("append_entry_log function1\n");
		logoffset += sizeof(struct fuckfs_extent);
	}else{
		//printf("append_entry_log %d\n",(logoffset&(g_block_size_bytes-1))+sizeof(struct fuckfs_dentry));
		if((logoffset&(g_block_size_bytes-1))+sizeof(struct fuckfs_dentry) >g_block_size_bytes){
			//printf("align log\n");
			memset(logaddr,0,g_block_size_bytes-(logoffset&(g_block_size_bytes-1)));
			logoffset=(logoffset&(~(g_block_size_bytes-1))) +g_block_size_bytes;
			logaddr= logstart+logoffset;
			//printf("offset %d\n", logoffset);
		}
		//printf("append_entry_log function %lu, %lu, %lu\n",logaddr,logstart,logend);
		memcpy(logaddr, entry, sizeof(struct fuckfs_dentry));
		//printf("fuckfs_dentry->entry_type %d\n",entry->entry_type);
		msync(logaddr,sizeof(struct fuckfs_dentry), MS_SYNC);

		//printf("append_entry_log function2\n");
		logoffset += sizeof(struct fuckfs_dentry);
	}
	pthread_mutex_unlock(applog_lock);
	//printf("end append_entry_log function %lu\n",logaddr);
}

//extern uint32_t allocate_NVMBlock =64;
//extern uint32_t allocate_SSDBlock = 64*1024;
/*
static void allocate_block(struct inode* inode, uint32_t allocate_NVMBlock, uint32_t allocate_SSDBlock){
	uint32_t i, size, allnvm,allssd;
	allnvm = allocate_NVMBlock;
	allssd = allocate_SSDBlock;
	struct space *tmpspace;
	//struct space nvmspace,ssdspace;
	//INIT_LIST_HEAD(&nvmspace.list);
	//INIT_LIST_HEAD(&ssdspace.list);
	for(i=0;i<nrangenum;i++){
		size = nblock_table[i][1]-nblock_table[i][0];
		tmpspace =(struct space*)malloc(sizeof(struct space));
		if(size>=allnvm){
			tmpspace->startaddr = nblock_table[i][0];
			tmpspace->endaddr = nblock_table[i][0]+allnvm;
			nblock_table[i][0] = nblock_table[i][0]+allnvm;
			tmpspace->size = allnvm;
			allnvm = 0;
		}else{  //size < allnvm
			tmpspace->startaddr = nblock_table[i][0];
			tmpspace->endaddr = nblock_table[i][1];
			tmpspace->size =size;
			allnvm = allnvm -size;
			nblock_table[i][0] = 0;
			nblock_table[i][1] = 0;
		}
		list_add_tail(&(tmpspace->list),&(inode->nvmspace->list));
		if(allnvm<=0){
			break;
		}
	}
	inode->nvmsize = allocate_NVMBlock - allnvm;
	for(i=0;i<srangenum;i++){
		size = sblock_table[i][1]-sblock_table[i][0];
		tmpspace =(struct space*)malloc(sizeof(struct space));
		if(size>=allssd){
			tmpspace.startaddr = sblock_table[i][0];
			tmpspace.endaddr = sblock_table[i][0]+allssd;
			nblock_table[i][0] = sblock_table[i][0]+allssd;
			tmpspace.size = allssd;
			allnvm = 0;
		}else{  //size < allnvm
			tmpspace.startaddr = sblock_table[i][0];
			tmpspace.endaddr = sblock_table[i][1];
			tmpspace.size =size;
			allssd = allssd -size;
			sblock_table[i][0] = 0;
			sblock_table[i][1] = 0;
		}
		list_add_tail(&tmpspace.list,&inode->ssdspace.list);
		if(allssd<=0){
			break;
		}
	}
	inode->nvmsize = allocate_SSDBlock - allssd;
	
	//inode->nvmspace =&nvmspace;
	//inode->ssdspace = &ssdspace;
}

*/
//获取ino, filetype
//理想情况，获取这个dir的所有dentry,包括ino,filetype, and filename
static struct ufs_direntry * fetch_ondisk_dentry_from_kernel(struct inode *inode, char * filename){
	//userfs_printf("fetch_ondisk_dentry_from_kernel filename %s, ino %d\n",filename, inode->inum);
	uint64_t ino_ftype;
	struct ufs_direntry *ude;
	uint64_t suffix =3;
	ino_ftype=syscall(338, inode->inum, filename, strlen(filename));
	//userfs_info("filename %s, ino_ftype %lu\n",filename, ino_ftype);
	if(ino_ftype>0){
		ude = (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
		struct fs_direntry *de = (struct fs_direntry *)malloc(sizeof(struct fs_direntry));
		de->ino= ino_ftype & (~ suffix);
		de->file_type= ino_ftype & suffix;
		//printf("de->ino %ld\n",de->ino);
		//printf("de->filetype %d\n",de->file_type);
		de->name_len = strlen(filename);
		strcpy(de->name, filename);
       	ude->addordel = Ondisk;
		ude->direntry=de;
		//userfs_info("art_insert filename is %s, inode->inum %d\n", filename,inode->inum);
		pthread_rwlock_wrlock(&inode->art_rwlock);
		art_insert(&inode->dentry_tree, filename,de->name_len,ude);
		pthread_rwlock_unlock(&inode->art_rwlock);
	}else{
		return NULL;
	}
	//userfs_info("ude is %lu\n",ude);
	return ude;
//inode->inum, filename
}


static int fetch_ondisk_dentry(struct inode *inode){
	//printf("fetch_ondisk_dentry %s\n",filename);
	userfs_printf("inode->root is %d\n",inode->root);
	char *blk_base= (char *)malloc(4096*sizeof(char));
	memcpy(blk_base, mmapstart+inode->root, 4096);
	//printf("blk_base %s\n", blk_base);
	struct fs_direntry *de;

	int rlen;
	de = (struct fs_direntry *)blk_base;
	//printf("de->ino is %d\n",de->ino);
	//printf("de->name is %s\n",de->name);
	char *top = blk_base + 4096;
	while ((char *)de <= top) {
		char dename[256];
		//userfs_info("de is %lu\n",de);
		//userfs_info("top is %lu\n",top);
		//获取的旧数据
       // userfs_info("dentry ino is %d\t %d\t %s\n", de->ino, de->de_len,de->name);
        //userfs_info("de->name_len %d\n", de->name_len);
        memmove(dename,de->name,de->name_len);
        dename[de->name_len]='\0';
        //userfs_info("deneme is %s\n", dename);
        rlen = le16_to_cpu(de->de_len);
        //if(rlen>4000){
        //	printf("rlen>4000\n");
        //	rlen=16;
        //}
        if (de->ino && rlen!=0) {
        	struct ufs_direntry *ude = (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
        	ude->addordel = Ondisk;
			ude->direntry=de;
			//userfs_printf("art_insert deneme is %s, de->name_len %d\n", dename,de->name_len);
			pthread_rwlock_wrlock(&inode->art_rwlock);
			art_insert(&inode->dentry_tree, dename,de->name_len,ude);
			pthread_rwlock_unlock(&inode->art_rwlock);
        	de = (struct fs_direntry *)((char *)de + rlen);

            //nlen = UFS_DIR_REC_LEN(de->name_len);
            //printf("nlen is %d\n",nlen);
           	//printf("rlen is %d\n",rlen);
        }else
           	break;
        
    }
    if ((char *)de > top)
        return -1;

    return 0;
}


#define inodebase 6291456  //如何根據ino獲取inode
static struct inode *fetch_ondisk_inode_ino(uint32_t ino)
{
	userfs_info("fetch_ondisk_inode_ino ino %d\n", ino);
	u64 inodeoff;
	char cinode[128];
	struct pmfs_inode * pinode;
	//inodeoff= ino << inode_shift;
	inodeoff= ino;
	memcpy(cinode,mmapstart+ inodebase + inodeoff, 128);
	pinode = (struct pmfs_inode *)cinode;
	struct inode *inode = icache_alloc_add(ino);

	//inode->inum = ino;
	inode->height =pinode->height;
	inode->i_blk_type = pinode->i_blk_type;
	//inode->flags = pinode->i_flags;
	inode->flags |= I_VALID;
	inode->root = pinode->root;
	inode->size=pinode->i_size;
	inode->ctime.tv_sec= pinode->i_ctime;            
	inode->mtime.tv_sec=pinode->i_mtime;           
	inode->i_mode=pinode->i_mode;             
	inode->nlink=pinode->i_links_count;     
	inode->blocks=pinode->i_blocks;           
	inode->atime.tv_sec=pinode->i_atime;           
	if(S_ISDIR(inode->i_mode))
    	inode->itype=T_DIR;
    else if(S_ISREG(inode->i_mode))
    	inode->itype=T_FILE;
    else
    	inode->itype=T_DEV;
	//userfs_info("inode->size is %d, inode->ctime.tv_sec is %d\n", inode->size, inode->ctime.tv_sec);
	//userfs_info("pinode->height is %d, root is %d\n", pinode->height, pinode->root);

	inode->de_cache = NULL;
	pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&inode->i_mutex, NULL);
	return inode;
}


static struct inode * inodecopy(struct tmpinode * tinode)
{
	//userfs_info("inodecopy function %lu, %lu, %lu\n",logaddr,logstart,logend);
	struct inode *inode= icache_alloc_add(tinode->i_ino);
	//printf("icache_alloc_add\n");
	//struct inode *
	//uint32_t ino =tinode->i_ino;
	//inode = icache_alloc_add(ino);
	//struct inode *inode =(struct inode *)malloc(sizeof(struct inode));
	if(!inode){
		panic("inode is null\n");
	}
	inode->i_mode = tinode->i_mode;
    //inode->i_opflags = tinode->i_opflags;
    //inode->i_uid = tinode->i_uid;
    //inode->i_gid = tinode->i_gid;
    inode->flags = tinode->i_flags;
    //inode->inum = tinode->i_ino;
    inode->nlink = tinode->i_nlink;
    inode->size = tinode->i_size;
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

    if(S_ISDIR(inode->i_mode))
    	inode->itype=T_DIR;
    else if(S_ISREG(inode->i_mode))
    	inode->itype=T_FILE;
    else
    	inode->itype=T_DEV;
    //inode->i_blkbits = tinode->i_blkbits;
    inode->blocks = tinode->i_blocks;
    //inode->i_state = tinode->i_state;
	inode->height = tinode->height;
    inode->root = tinode->root;
    inode->i_blk_type = tinode->i_blk_type;
    inode->de_cache = NULL;
    inode->nvmblock = tinode->nvmblock;
    inode->usednvms=tinode->nvmblock;
	pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&inode->i_mutex, NULL);
	return inode;
}

static inline struct inode *icache_find( uint32_t inum)
{
	struct inode *inode;

	//userfs_assert(dev == g_root_dev);

	pthread_rwlock_rdlock(icache_rwlock);

	HASH_FIND(hash_handle, inode_hash, &inum,
        		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline struct inode *icache_alloc_add(uint32_t inum)
{
	//printf("icache_alloc_add\n");
	struct inode *inode;
	pthread_rwlockattr_t rwlattr;

	//userfs_assert(dev == g_root_dev);

	inode = (struct inode *)malloc(sizeof(*inode));

	if (!inode)
		panic("Fail to allocate inode\n");
	//printf("inode\n");
	//inode->dev = dev;
	inode->inum = inum;
	inode->i_ref = 1;
	//printf("inode set\n");
	//inode->_dinode = (struct dinode *)inode;

#if 0
	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_rwlock_init(&inode->fcache_rwlock, &rwlattr);
	inode->fcache = NULL;
	inode->n_fcache_entries = 0;

#ifdef KLIB_HASH
	userfs_info("allocate hash %u\n", inode->inum);
	inode->fcache_hash = kh_init(fcache);
#endif

	INIT_LIST_HEAD(&inode->i_slru_head);
	
	pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	inode->de_cache = NULL;
#endif
	//printf("pthread_rwlock_wrlock\n");
	pthread_rwlock_wrlock(icache_rwlock);
	//printf("HASH_ADD\n");
	HASH_ADD(hash_handle, inode_hash, inum,
	 		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline struct inode *icache_add(struct inode *inode)
{
	uint32_t inum = inode->inum;
	//printf("inum %d\n", inum);
	pthread_mutex_init(&inode->i_mutex, NULL);
	
	pthread_rwlock_wrlock(icache_rwlock);

	HASH_ADD(hash_handle, inode_hash, inum,
	 		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);
	//printf("inode->inum %d\n", inode->inum);
	return inode;
}

static inline int icache_del(struct inode *ip)
{
	pthread_rwlock_wrlock(icache_rwlock);

	HASH_DELETE(hash_handle, inode_hash, ip);

	pthread_rwlock_unlock(icache_rwlock);

	return 0;
}

#ifdef KLIB_HASH
static inline struct fcache_block *fcache_find(struct inode *inode, offset_t key)
{
	//printf("in fcache_find1 %d\n",key);
	khiter_t k;
	struct fcache_block *fc_block = NULL;

	pthread_rwlock_rdlock(&inode->fcache_rwlock);

	//printf("kh_get %lx\n",inode->fcache_hash);
	k = kh_get(fcache, inode->fcache_hash, key);
	//printf("kh_end k %ld,kh_end() %ld\n",k,kh_end(inode->fcache_hash));
	if (k == kh_end(inode->fcache_hash)) {
		pthread_rwlock_unlock(&inode->fcache_rwlock);
		//printf("return NULL\n");
		return NULL;
	}
	//printf("kh_value %ld\n",k);
	fc_block = kh_value(inode->fcache_hash, k);

	pthread_rwlock_unlock(&inode->fcache_rwlock);
	//printf("fc_block %lx\n", fc_block);
	return fc_block;
}

static inline struct fcache_block *fcache_alloc_add(struct inode *inode, 
		offset_t key, addr_t log_addr)
{
	//printf("fcache_alloc_add\n");
	struct fcache_block *fc_block;
	khiter_t k;
	int ret;

	fc_block = (struct fcache_block *)userfs_zalloc(sizeof(*fc_block));
	if (!fc_block)
		panic("Fail to allocate fcache block\n");

	fc_block->key = key;
	fc_block->log_addr = log_addr;
	fc_block->invalidate = 0;
	fc_block->is_data_cached = 0;
	fc_block->inum = inode->inum;
	inode->n_fcache_entries++;
	INIT_LIST_HEAD(&fc_block->l);

	pthread_rwlock_wrlock(&inode->fcache_rwlock);
	
	k = kh_put(fcache, inode->fcache_hash, key, &ret);
	if (ret < 0)
		panic("fail to insert fcache value");
	/*
	else if (!ret) {
		kh_del(fcache, inode->fcache_hash, k);
		k = kh_put(fcache, inode->fcache_hash, key, &ret);
	}
	*/

	kh_value(inode->fcache_hash, k) = fc_block;
	//userfs_info("add key %u @ inode %u\n", key, inode->inum);

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline int fcache_del(struct inode *inode, 
		struct fcache_block *fc_block)
{
	khiter_t k;
	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	k = kh_get(fcache, inode->fcache_hash, fc_block->key);

	if (kh_exist(inode->fcache_hash, k)) {
		kh_del(fcache, inode->fcache_hash, k);
		inode->n_fcache_entries--;
	}

	/*
	if (k != kh_end(inode->fcache_hash)) {
		kh_del(fcache, inode->fcache_hash, k);
		inode->n_fcache_entries--;
		userfs_debug("del key %u @ inode %u\n", fc_block->key, inode->inum);
	}
	*/

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return 0;
}

static inline int fcache_del_all(struct inode *inode)
{
	khiter_t k;
	struct fcache_block *fc_block;

	for (k = kh_begin(inode->fcache_hash); 
			k != kh_end(inode->fcache_hash); k++) {
		if (kh_exist(inode->fcache_hash, k)) {
			fc_block = kh_value(inode->fcache_hash, k);

			if (fc_block->is_data_cached) {
				list_del(&fc_block->l);
				userfs_free(fc_block->data);
			}
			//userfs_free(fc_block);
		}
	}

	userfs_debug("destroy hash %u\n", inode->inum);
	kh_destroy(fcache, inode->fcache_hash);
	return 0;
}
// UTHash version
#else
static inline struct fcache_block *fcache_find(struct inode *inode, offset_t key)
{
	//printf("in fcache_find2\n");
	struct fcache_block *fc_block = NULL;

	pthread_rwlock_rdlock(&inode->fcache_rwlock);

	HASH_FIND(hash_handle, inode->fcache, &key,
        		sizeof(offset_t), fc_block);
	
	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline struct fcache_block *fcache_alloc_add(struct inode *inode, 
		offset_t key, addr_t log_addr)
{
	//printf("fcache_alloc_add\n");
	struct fcache_block *fc_block;

	fc_block = (struct fcache_block *)userfs_zalloc(sizeof(*fc_block));
	if (!fc_block)
		panic("Fail to allocate fcache block\n");

	fc_block->key = key;
	fc_block->inum = inode->inum;
	fc_block->log_addr = log_addr;
	fc_block->invalidate = 0;
	fc_block->is_data_cached = 0;
	inode->n_fcache_entries++;
	INIT_LIST_HEAD(&fc_block->l);

	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	HASH_ADD(hash_handle, inode->fcache, key,
	 		sizeof(offset_t), fc_block);

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline int fcache_del(struct inode *inode, 
		struct fcache_block *fc_block)
{
	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	HASH_DELETE(hash_handle, inode->fcache, fc_block);
	inode->n_fcache_entries--;

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return 0;
}

static inline int fcache_del_all(struct inode *inode)
{
	struct fcache_block *item, *tmp;

	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	HASH_ITER(hash_handle, inode->fcache, item, tmp) {
		HASH_DELETE(hash_handle, inode->fcache, item);
		if (item->is_data_cached) {
			list_del(&item->l);
			userfs_free(item->data);
		}
		userfs_free(item);
	}
	HASH_CLEAR(hash_handle, inode->fcache);

	inode->n_fcache_entries = 0;

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return 0;
}
#endif

static inline struct inode *de_cache_find(struct inode *dir_inode, 
		const char *_name)
{
	//printf("de_cache_find name %s\n", _name);
	struct dirent_data *dirent_data;
	//printf("dir_inode de_cache %lu\n", dir_inode->de_cache);
	HASH_FIND_STR(dir_inode->de_cache, _name, dirent_data);

	
	if (dirent_data) {
		//*offset = dirent_data->offset;
		//userfs_info("dirent_data->name %s\n", dirent_data->name);
		return dirent_data->inode;
	} else {
		//*offset = 0;
		return NULL;
	}
}

static inline struct inode *de_cache_alloc_add(struct inode *dir_inode, 
		const char *name, struct inode *inode)
{
	//printf("de_cache_alloc_add dir_inode ino %d, name %s, inode ino %d\n",dir_inode->inum, name,inode->inum);
	struct dirent_data *_dirent_data;

	_dirent_data = (struct dirent_data *)malloc(sizeof(*_dirent_data));
	if (!_dirent_data)
		panic("Fail to allocate dirent data\n");

	//printf("de_cache_alloc_add1\n");
	strcpy(_dirent_data->name, name);

	_dirent_data->inode = inode;
	//_dirent_data->offset = _offset;
	//printf("de_cache_alloc_add2 %lu\n",dir_inode->de_cache_spinlock);
	pthread_spin_lock(&dir_inode->de_cache_spinlock);
	//printf("de_cache_alloc_add3\n");
	HASH_ADD_STR(dir_inode->de_cache, name, _dirent_data);
	//printf("de_cache_alloc_add4\n");

	dir_inode->n_de_cache_entry++;

	pthread_spin_unlock(&dir_inode->de_cache_spinlock);

	
	return dir_inode;
}

static inline int de_cache_del(struct inode *dir_inode, const char *_name)
{
	//printf("de_cache_del dir_inode ino %d, name %s\n",dir_inode->inum,_name);
	struct dirent_data *dirent_data;

	HASH_FIND_STR(dir_inode->de_cache, _name, dirent_data);
	if (dirent_data) {
		pthread_spin_lock(&dir_inode->de_cache_spinlock);
		HASH_DEL(dir_inode->de_cache, dirent_data);
		dir_inode->n_de_cache_entry--;
		pthread_spin_unlock(&dir_inode->de_cache_spinlock);
	}

	return 0;
}

static inline struct inode *dlookup_find(const char *path)
{
	userfs_info("dlookup_find path %s, dlookup_rwlock %lu\n",path,dlookup_rwlock);
	struct dlookup_data *_dlookup_data;

	pthread_rwlock_rdlock(dlookup_rwlock);

	HASH_FIND_STR(dlookup_hash, path, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);

	
	if (!_dlookup_data){
		return NULL;
	}
	else{
		//userfs_info("dlookup_find _dlookup_data %lu, inode %lu\n", _dlookup_data,_dlookup_data->inode);
		return _dlookup_data->inode;
	}
}

static inline struct inode *dlookup_alloc_add(	struct inode *inode, const char *_path)
{
	//userfs_info("dlookup_alloc_add %s, inode %lu, \n", _path,inode);
	struct dlookup_data *_dlookup_data;

	_dlookup_data = (struct dlookup_data *)userfs_zalloc(sizeof(*_dlookup_data));
	if (!_dlookup_data)
		panic("Fail to allocate dlookup data\n");
	
	strcpy(_dlookup_data->path, _path);

	_dlookup_data->inode = inode;

	//userfs_info("dlookup_alloc_add %s, inode %lu, _dlookup_data %lu, _dlookup_data->inode %lu\n", _path,inode,_dlookup_data,_dlookup_data->inode);
	pthread_rwlock_wrlock(dlookup_rwlock);

	HASH_ADD_STR(dlookup_hash, path, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);
	
	return inode;
}

static inline int dlookup_del(const char *path)
{
	//userfs_info("dlookup_del %s\n", path);
	struct dlookup_data *_dlookup_data;

	pthread_rwlock_wrlock(dlookup_rwlock);

	HASH_FIND_STR(dlookup_hash, path, _dlookup_data);
	if (_dlookup_data)
		HASH_DEL(dlookup_hash, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);

	return 0;
}

// global variables
extern uint8_t fs_dev_id;
extern struct disk_superblock *disk_sb;
extern struct super_block *sb;

//forward declaration
struct fs_stat;

void shared_slab_init(uint8_t shm_slab_index);

void read_superblock(uint8_t dev);
void read_root_inode(uint8_t dev);
void load_files(struct inode * inode);
void load_blktree(struct inode *inode);
//int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip);
//int sync_inode_ext_tree(uint8_t dev, struct inode *inode);
struct inode* icreate( uint8_t type);
struct inode* ialloc( uint32_t inum);
int idealloc(struct inode *inode);
struct inode* idup(struct inode*);
struct inode* iget( uint32_t inum);
void ilock(struct inode*);
void iput(struct inode*);
void iunlock(struct inode*);
void iunlockput(struct inode*);
void iupdate(struct inode*);
int itrunc(struct inode *inode, offset_t length);
int bmap(struct inode *ip, struct bmap_request *bmap_req);

int dir_check_entry_fast(struct inode *dir_inode);
struct inode* dir_lookup(struct inode*, char*, struct ufs_direntry *);
int dir_get_entry(struct inode *dir_inode, struct linux_dirent *buf, offset_t off);
int dir_add_entry(struct inode *inode, char *name, uint32_t inum);
int dir_remove_entry(struct inode *inode,char *name, uint32_t inum);
int dir_change_entry(struct inode *dir_inode, char *oldname, char *newname);
int namecmp(const char*, const char*);
struct inode* namei(char*);
struct inode* nameiparent(char*, char*);
int readi_unopt(struct inode*, uint8_t *, offset_t, uint32_t);
int readi(struct inode*, uint8_t *, offset_t, uint32_t);
void stati(struct inode*, struct stat *);
//int add_to_log(struct inode*, uint8_t*, offset_t, uint32_t);
//int check_log_invalidation(struct fcache_block *_fcache_block);
//uint8_t *get_dirent_block(struct inode *dir_inode, offset_t offset);
void show_libfs_stats(void);

//APIs for debugging.
uint32_t dbg_get_iblkno(uint32_t inum);
void dbg_dump_inode(uint32_t inum);
void dbg_check_inode(void *data);
void dbg_check_dir(void *data);
void dbg_dir_dump(uint8_t dev, uint32_t inum);
void dbg_path_walk(char *path);

// mempool slab for libfs
extern ncx_slab_pool_t *userfs_slab_pool;
// mempool on top of shared memory
extern ncx_slab_pool_t *userfs_slab_pool_shared;
extern uint8_t shm_slab_index;

extern pthread_rwlock_t *shm_slab_rwlock; 
extern pthread_rwlock_t *shm_lru_rwlock; 

extern uint64_t *bandwidth_consumption;

// Inodes per block.
//#define IPB           (g_block_size_bytes / sizeof(struct dinode))

// Block containing inode i
/*
#define IBLOCK(i, disk_sb)  ((i/IPB) + disk_sb.inode_start)

static inline addr_t get_inode_block(uint8_t dev, uint32_t inum)
{
	return (inum / IPB) + disk_sb[dev].inode_start;
}
*/
// Bitmap bits per block
#define BPB           (g_block_size_bytes*8)

// Block of free map containing bit for block b
#define BBLOCK(b, disk_sb) (b/BPB + disk_sb.bmap_start)

#ifdef __cplusplus
}
#endif

#endif
