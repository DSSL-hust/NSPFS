#ifndef _FS_H_
#define _FS_H_

#include "global/global.h"
#include "global/types.h"
#include "global/defs.h"
#include "global/mem.h"
#include "global/ncx_slab.h"
#include "stat.h"
#include "shared.h"
#include "io/buffer_head.h"
#include "io/block_io.h"
#include "io/device.h"
#include "ds/uthash.h"
#include "ds/khash.h"
#include "filesystem/lru.h"
#include "threadpool/thpool.h"
#include <sys/mman.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline struct inode *icache_alloc_add(uint32_t inum);


typedef unsigned short		umode_t;
typedef unsigned int	uid_t;
typedef unsigned int	gid_t;

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
	int i;
	uint32_t ino;
	pthread_mutex_lock(bitmap_lock);
	for(i =bitmapnum;i<inonum;i++){
		if(fsinode_table[i]!=0){
			ino= fsinode_table[i];
			fsinode_table[i]=0;
			bitmapnum=i+1;
			pthread_mutex_unlock(bitmap_lock);
			return ino;
		}
	}
	pthread_mutex_unlock(bitmap_lock);
	return 0;
}

static uint32_t put_inode_bitmap(uint32_t ino){
	int i;
	pthread_mutex_lock(bitmap_lock);
	for(i =bitmapnum-1;i>=0;i--){
		if(fsinode_table[i]==0){
			fsinode_table[i]=ino;
			bitmapnum=i;
			pthread_mutex_unlock(bitmap_lock);
			return ino;
		}
	}
	pthread_mutex_unlock(bitmap_lock);
	return 0;
}


#define NVMSIZE 10*1024*1024 //40GB
static uint64_t allocate_nvmblock(uint64_t blocks){
	uint64_t alloc_nspace;
	pthread_mutex_lock(allonvm_lock);
	if(ADDRINFO->nvm_block_addr+blocks<=NVMSIZE){
		alloc_nspace = ADDRINFO->nvm_block_addr <<g_block_size_shift; //4K 地址-->字節地址
		//size =blocks;
		ADDRINFO->nvm_block_addr+=blocks;
		
	}else if(ADDRINFO->nvm_block_addr+blocks>NVMSIZE){
		ADDRINFO->nvm_block_addr = nvmstartblock;
		alloc_nspace = ADDRINFO->nvm_block_addr <<g_block_size_shift; //4K 地址-->字節地址
		ADDRINFO->nvm_block_addr+=blocks;

		
	}
	pthread_mutex_unlock(allonvm_lock);
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
	uint64_t allospace;
	pthread_mutex_lock(allossd1_lock);
	allospace=ADDRINFO->ssd1_block_addr<<21; //2M block address  --> byte address
	ADDRINFO->ssd1_block_addr+=1;  //2M地址
	pthread_mutex_unlock(allossd1_lock);
	return allospace;
}

static uint64_t allocate_ssd2block(){
	uint64_t allospace;
	pthread_mutex_lock(allossd2_lock);
	allospace=ADDRINFO->ssd2_block_addr<<21; //2M block address  --> byte address
	ADDRINFO->ssd2_block_addr+=1;  //2M地址
	pthread_mutex_unlock(allossd2_lock);
	return allospace;
}

static uint64_t allocate_ssd3block(){
	
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
	uint64_t flag=4095;
	uint64_t offset = logoffset;
	if ((logoffset&flag)>(4096-sizeof(struct fuckfs_addr_info))){
		offset = (logoffset >>page_shift +1)<<page_shift;
	}
	
	pthread_mutex_lock(applog_lock);
	logaddr = logstart + offset;
	ADDRINFO->free_inode_base = fsinode_table[bitmapnum];
#if 0
	ADDRINFO->ssd1_block_addr=(ADDRINFO->ssd1_block_addr+511)>>9;
	ADDRINFO->ssd2_block_addr=(ADDRINFO->ssd2_block_addr+511)>>9;
	ADDRINFO->ssd3_block_addr=(ADDRINFO->ssd3_block_addr+511)>>9;
#endif	
	memcpy(logaddr, ADDRINFO, sizeof(struct fuckfs_addr_info));
	msync(logaddr,sizeof(struct fuckfs_addr_info), MS_SYNC);
	pthread_mutex_unlock(applog_lock);
}

static void submit_log(){
	printf("submit_log logstart %ld, logbase %ld, logoffset %ld\n",logstart,logbase,logoffset);
}
static void append_entry_log(struct fuckfs_extent *entry, uint8_t flag){
	
	pthread_mutex_lock(applog_lock);
	if(logoffset+sizeof(struct fuckfs_dentry) > logspace){
		userfs_info("logoffset %d\n",logoffset);
		submit_log();
		logoffset=0;
	}
	logaddr = logstart + logoffset;

	if(flag==0){
		
		memcpy(logaddr, entry, sizeof(struct fuckfs_extent));
		
		msync(logaddr,sizeof(struct fuckfs_extent), MS_SYNC);
		
		logoffset += sizeof(struct fuckfs_extent);
	}else{
		
		if((logoffset&(g_block_size_bytes-1))+sizeof(struct fuckfs_dentry) >g_block_size_bytes){
			
			memset(logaddr,0,g_block_size_bytes-(logoffset&(g_block_size_bytes-1)));
			logoffset=(logoffset&(~(g_block_size_bytes-1))) +g_block_size_bytes;
			logaddr= logstart+logoffset;
		}
		memcpy(logaddr, entry, sizeof(struct fuckfs_dentry));
		msync(logaddr,sizeof(struct fuckfs_dentry), MS_SYNC);

		logoffset += sizeof(struct fuckfs_dentry);
	}
	pthread_mutex_unlock(applog_lock);
}

//获取ino, filetype
static struct ufs_direntry * fetch_ondisk_dentry_from_kernel(struct inode *inode, char * filename){
	uint64_t ino_ftype;
	struct ufs_direntry *ude;
	uint64_t suffix =3;
	ino_ftype=syscall(338, inode->inum, filename, strlen(filename));
	if(ino_ftype>0){
		ude = (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
		struct fs_direntry *de = (struct fs_direntry *)malloc(sizeof(struct fs_direntry));
		de->ino= ino_ftype & (~ suffix);
		de->file_type= ino_ftype & suffix;
		de->name_len = strlen(filename);
		strcpy(de->name, filename);
       	ude->addordel = Ondisk;
		ude->direntry=de;
		pthread_rwlock_wrlock(&inode->art_rwlock);
		art_insert(&inode->dentry_tree, filename,de->name_len,ude);
		pthread_rwlock_unlock(&inode->art_rwlock);
	}else{
		return NULL;
	}
	return ude;

}


static int fetch_ondisk_dentry(struct inode *inode){
	char *blk_base= (char *)malloc(4096*sizeof(char));
	memcpy(blk_base, mmapstart+inode->root, 4096);
	struct fs_direntry *de;

	int rlen;
	de = (struct fs_direntry *)blk_base;
	char *top = blk_base + 4096;
	while ((char *)de <= top) {
		char dename[256];
        memmove(dename,de->name,de->name_len);
        dename[de->name_len]='\0';
        rlen = le16_to_cpu(de->de_len);
       
        if (de->ino && rlen!=0) {
        	struct ufs_direntry *ude = (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
        	ude->addordel = Ondisk;
			ude->direntry=de;
			pthread_rwlock_wrlock(&inode->art_rwlock);
			art_insert(&inode->dentry_tree, dename,de->name_len,ude);
			pthread_rwlock_unlock(&inode->art_rwlock);
        	de = (struct fs_direntry *)((char *)de + rlen);

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
	
	inode->de_cache = NULL;
	pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&inode->i_mutex, NULL);
	return inode;
}


static struct inode * inodecopy(struct tmpinode * tinode)
{
	//userfs_info("inodecopy function %lu, %lu, %lu\n",logaddr,logstart,logend);
	struct inode *inode= icache_alloc_add(tinode->i_ino);
	if(!inode){
		panic("inode is null\n");
	}
	inode->i_mode = tinode->i_mode;
  
    inode->flags = tinode->i_flags;
    //inode->inum = tinode->i_ino;
    inode->nlink = tinode->i_nlink;
    inode->size = tinode->i_size;
    //时间类型不同
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


	pthread_rwlock_rdlock(icache_rwlock);

	HASH_FIND(hash_handle, inode_hash, &inum,
        		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline struct inode *icache_alloc_add(uint32_t inum)
{
	struct inode *inode;
	pthread_rwlockattr_t rwlattr;


	inode = (struct inode *)malloc(sizeof(*inode));

	if (!inode)
		panic("Fail to allocate inode\n");
	
	inode->inum = inum;
	inode->i_ref = 1;
	

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
	pthread_rwlock_wrlock(icache_rwlock);
	HASH_ADD(hash_handle, inode_hash, inum,
	 		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline struct inode *icache_add(struct inode *inode)
{
	uint32_t inum = inode->inum;
	pthread_mutex_init(&inode->i_mutex, NULL);
	
	pthread_rwlock_wrlock(icache_rwlock);

	HASH_ADD(hash_handle, inode_hash, inum,
	 		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);
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
	khiter_t k;
	struct fcache_block *fc_block = NULL;

	pthread_rwlock_rdlock(&inode->fcache_rwlock);

	k = kh_get(fcache, inode->fcache_hash, key);
	
	if (k == kh_end(inode->fcache_hash)) {
		pthread_rwlock_unlock(&inode->fcache_rwlock);
		
		return NULL;
	}
	fc_block = kh_value(inode->fcache_hash, k);

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline struct fcache_block *fcache_alloc_add(struct inode *inode, 
		offset_t key, addr_t log_addr)
{
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

	kh_value(inode->fcache_hash, k) = fc_block;

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
	struct dirent_data *dirent_data;
	HASH_FIND_STR(dir_inode->de_cache, _name, dirent_data);

	
	if (dirent_data) {
		
		return dirent_data->inode;
	} else {

		return NULL;
	}
}

static inline struct inode *de_cache_alloc_add(struct inode *dir_inode, 
		const char *name, struct inode *inode)
{
	struct dirent_data *_dirent_data;

	_dirent_data = (struct dirent_data *)malloc(sizeof(*_dirent_data));
	if (!_dirent_data)
		panic("Fail to allocate dirent data\n");

	strcpy(_dirent_data->name, name);

	_dirent_data->inode = inode;
	pthread_spin_lock(&dir_inode->de_cache_spinlock);
	
	HASH_ADD_STR(dir_inode->de_cache, name, _dirent_data);
	
	dir_inode->n_de_cache_entry++;

	pthread_spin_unlock(&dir_inode->de_cache_spinlock);

	
	return dir_inode;
}

static inline int de_cache_del(struct inode *dir_inode, const char *_name)
{
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
	struct dlookup_data *_dlookup_data;

	pthread_rwlock_rdlock(dlookup_rwlock);

	HASH_FIND_STR(dlookup_hash, path, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);

	
	if (!_dlookup_data){
		return NULL;
	}
	else{
		return _dlookup_data->inode;
	}
}

static inline struct inode *dlookup_alloc_add(	struct inode *inode, const char *_path)
{
	struct dlookup_data *_dlookup_data;

	_dlookup_data = (struct dlookup_data *)userfs_zalloc(sizeof(*_dlookup_data));
	if (!_dlookup_data)
		panic("Fail to allocate dlookup data\n");
	
	strcpy(_dlookup_data->path, _path);

	_dlookup_data->inode = inode;

	pthread_rwlock_wrlock(dlookup_rwlock);

	HASH_ADD_STR(dlookup_hash, path, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);
	
	return inode;
}

static inline int dlookup_del(const char *path)
{
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


// Bitmap bits per block
#define BPB           (g_block_size_bytes*8)

// Block of free map containing bit for block b
#define BBLOCK(b, disk_sb) (b/BPB + disk_sb.bmap_start)

#ifdef __cplusplus
}
#endif

#endif
