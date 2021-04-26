/* userfs file system interface
 *
 * Implement file-based operations with file descriptor and
 * struct file.
 */
#include "userfs/userfs_user.h"
#include "global/global.h"
#include "global/util.h"
#include "filesystem/fs.h"
#include "filesystem/file.h"
#include "concurrency/synchronization.h"
#include <math.h>
#include <pthread.h>
#include "ds/blktree.h"

struct open_file_table g_fd_table;

//static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;



void userfs_file_init(void)
{
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_spin_init(&g_fd_table.lock, PTHREAD_PROCESS_SHARED);
	
	struct file *f= &g_fd_table.open_files[0];
	for(int i = 0; i < g_max_open_files; i++, f++) {
		memset(f, 0, sizeof(*f));
		pthread_rwlock_init(&f->rwlock, &rwlattr);
	}

}


// Allocate a file structure.
/* FIXME: the ftable implementation is too naive. need to
 * improve way to allocate struct file */



struct file* userfs_file_alloc(void)
{
	int i = 0;
	
	struct file *f;
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);
	pthread_spin_lock(&g_fd_table.lock);
	for(i = 0, f = &g_fd_table.open_files[0]; 
			i < g_max_open_files; i++, f++) {
		
		//
		if(f->ref == 0) {
			pthread_rwlock_wrlock(&f->rwlock);
			f->ref = 1;
			f->fd = i;
			pthread_rwlock_unlock(&f->rwlock);
			pthread_spin_unlock(&g_fd_table.lock);
			return f;
		}else{
			
		}
	}

	pthread_spin_unlock(&g_fd_table.lock);
	return 0;
}

// Increment ref count for file f.
struct file* userfs_file_dup(struct file *f)
{

	panic("not supported\n");

	pthread_rwlock_wrlock(&f->rwlock);
	
	if(f->ref < 1)
		panic("filedup");
	f->ref++;

	pthread_rwlock_unlock(&f->rwlock);
	
	return f;
}

int userfs_file_close(struct file *f)
{
	//userfs_info("userfs_file_close filename %s\n",f->filename);
	struct file ff;

	
	userfs_assert(f->ref > 0);

	struct  inode * inode = f->ip;
	pthread_rwlock_wrlock(&f->rwlock);

	if(--f->ref > 0) {
		if(f->type==FD_INODE)
			iput(inode);
		
		pthread_rwlock_unlock(&f->rwlock);
		return 0;
	}
	
	ff = *f;
	
	f->type = FD_NONE;
	f->ref = 0;
	

	pthread_rwlock_unlock(&f->rwlock);


	if(ff.type == FD_INODE) 
		iput(ff.ip);

	return 0;
}

// Get metadata about file f.
int userfs_file_stat(struct file *f, struct stat *st)
{
	if(f->type == FD_INODE){
		ilock(f->ip);
		stati(f->ip, st);
		iunlock(f->ip);
		return 0;
	}
	return -1;
}

// Read from file f.
int userfs_file_read(struct file *f, uint8_t *buf, int n)
{
	//userfs_printf("userfs_file_read off %d size %d\n",f->off,n);
	

	if (f->readable == 0) 
		return -EPERM;
	if (f->type == FD_INODE) {
		ilock(f->ip);
		userfs_info("userfs_file_read f->off %d, f->ip->size %d, read size %d, inum %d\n",f->off,f->ip->size,n,f->ip->inum);
		if (f->off >= f->ip->size) {
			iunlock(f->ip);
			return 0;
		}
		
		
		int r = readi(f->ip, buf, f->off, n);
		
		if (r < 0) 
			panic("read error\n");

		f->off += r;

		iunlock(f->ip);
		return r;
	}
	
	panic("userfs_file_read\n");

	return -1;
}

int userfs_file_read_offset(struct file *f, uint8_t *buf, size_t n, offset_t off)
{
	int r;
	//printf("userfs_file_read_offset\n");
	if (f->readable == 0)
		return -EPERM;

	if (f->type == FD_INODE) {
		ilock(f->ip);

		if (f->off >= f->ip->size) {
			iunlock(f->ip);
			return 0;
		}

		r = readi(f->ip, buf, off, n);

		if (r < 0) 
			panic("read error\n");

		iunlock(f->ip);
		return r;
	}
	//lru_upsert(f->ip->inum,f->filename);

	return -1;
}


#define NVM_SIZE 6*1024*1024
uint64_t allocatenvmblock(uint64_t blocks, struct inode *ip){
	uint64_t alloc_nspace, size;
	//printf("ADDRINFO->nvm_block_addr %d\n", ADDRINFO->nvm_block_addr);
	pthread_mutex_lock(allonvm_lock);
	if(ADDRINFO->nvm_block_addr+blocks<=NVMSIZE){
		ip->nvmoff = ADDRINFO->nvm_block_addr<<g_block_size_shift; //字節地址
		size =blocks;
		ADDRINFO->nvm_block_addr+=size;
		
	}else if(ADDRINFO->nvm_block_addr<NVMSIZE){
		ip->nvmoff = ADDRINFO->nvm_block_addr<<g_block_size_shift; //字節地址
		size =NVMSIZE-ADDRINFO->nvm_block_addr;
		ADDRINFO->nvm_block_addr+=size;
		
	}else{
		ip->nvmoff=0;
		size =0;
	}
	pthread_mutex_unlock(allonvm_lock);
	//printf("ADDRINFO->nvm_block_addr %d\n", ADDRINFO->nvm_block_addr);
	return size;
}

int fs_nvmwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_info("fs_nvmwrite inode %lu, filesize %d, off %d, size %d\n",ip, ip->size, off, size);
	struct buffer_head *bh;
	struct fuckfs_extent * fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));;
	struct timeval curtime;
	unsigned long writeoff, fileoff;
	uint32_t writesize=size;
	uint64_t allosize;
	if(ip->nvmblock==0){
		
		allosize=allocatenvmblock(FILENVMBLOCK - ip->nvmblock,ip); 
		if(allosize==0)
			return -1;
		ip->nvmblock = FILENVMBLOCK;
		ip->nvmoff+=off&((FILENVMBLOCK<<g_block_size_shift) -1);
	}

	writeoff= ip->nvmoff >>g_block_size_shift;
	bh = bh_get_sync_IO(1,ip->nvmoff>>12,BH_NO_DATA_ALLOC);
	bh->b_data=buf;
	bh->b_size=size;
	bh->b_offset = off&((1<<g_block_size_shift) -1);

	userfs_write(bh);
	
	fsextent->device_id = 0;

	
	fsextent->ino = ip->inum;
	fsextent->ext_start_hi = (ip->nvmoff)>>32;  //字節地址
	fsextent->ext_start_lo = ip->nvmoff;
	fsextent->ext_len = size + (((uint32_t) 0x1<<g_block_size_shift) -1) >>g_block_size_shift;
	fsextent->ext_block = off >> g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	ip->nvmoff +=size;
	fsextent->blkoff_hi = ip->nvmoff>>32;
	fsextent->blkoff_lo = ip->nvmoff;
	append_entry_log(fsextent,0);

	fileoff = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	
	while(writesize >0){
		
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		struct block_node *blocknode =block_search(&(ip->blktree),fileoff);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		
		if(!blocknode){ //NULL
			blocknode=(struct block_node *)malloc(sizeof(struct block_node));
			
			blocknode->devid =1;
			blocknode->fileoff=fileoff;
			if(writesize>g_block_size_bytes){
				blocknode->size=g_block_size_bytes;
				writesize-=g_block_size_bytes;
			}else{
				blocknode->size=writesize;
				writesize=0;
			}
			blocknode->addr= writeoff;
			
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),blocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}else{//not NULL
			
			blocknode->devid =1;
			blocknode->fileoff=fileoff;
			if((blocknode->size+writesize)>g_block_size_bytes){
				blocknode->size=g_block_size_bytes;
				writesize-=(g_block_size_bytes-blocknode->size);
			}else{
				blocknode->size= blocknode->size+writesize;
				writesize=0;
			}
			blocknode->addr= writeoff;
			

		}
		writeoff++;
		fileoff+=g_block_size_bytes;
		
	}
	bh_release(bh);
	//userfs_info("nvm write success %d\n",size);
	return size;
}



uint64_t allocate_ssdblock(uint8_t devid){
	
	uint64_t allospace;
	pthread_mutex_lock(allossd_lock);
	switch (devid){
		case 1:
			allospace=ADDRINFO->ssd1_block_addr<<21;
			ADDRINFO->ssd1_block_addr+=1;  //2M地址

			break;
		case 2:
			allospace=ADDRINFO->ssd2_block_addr<<21;
			ADDRINFO->ssd2_block_addr+=1;
			break;
		case 3:
			allospace=ADDRINFO->ssd3_block_addr<<21;
			ADDRINFO->ssd3_block_addr+=1;
	}
	pthread_mutex_unlock(allossd_lock);
	return allospace;
}



int fs_ssdwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	uint64_t devid = ip->devid;
	
	struct buffer_head *bh;

	struct fuckfs_extent * fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));;
	struct timeval curtime;
	//int isnvm;
	unsigned long writeoff, fileoff;
	uint32_t writesize=size;
	offset_t off_aligned_2M = off &(~(((uint64_t)0x1<<g_lblock_size_shift) -1));

	if((ip->ssdoff<<43)==0 || ip->ssdoff >= ssd_size[ip->devid]){
		
		ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
		ip->ssdoff+=off&((1<<g_lblock_size_shift) -1);
	}
	writeoff= ip->ssdoff >>g_block_size_shift;

	bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
	bh->b_data=buf;
	bh->b_size=size;
	bh->b_offset=off&((1<<g_block_size_shift) -1);
	
	userfs_write(bh);
	
	
	bh_release(bh);

	fsextent->device_id = ip->devid;

	fsextent->ino = ip->inum;
	fsextent->ext_start_hi = ip->ssdoff>>32; //字節地址
	fsextent->ext_start_lo = ip->ssdoff;
	fsextent->ext_block = off >>g_block_size_shift;
	fsextent->ext_len = size + (((uint32_t) 0x1<<g_block_size_shift) -1) >>g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	ip->ssdoff+=size;
	fsextent->blkoff_hi = ip->ssdoff>>32;
	fsextent->blkoff_lo = ip->ssdoff;
	
	append_entry_log(fsextent,0);

	fileoff = off & ~(((uint64_t)0x1<<g_block_size_shift) -1);

	if(off_aligned_2M == off){//如果
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		struct block_node *blocknode =block_search(&(ip->blktree),off);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		if(!blocknode){ //NULL
			blocknode=(struct block_node *)malloc(sizeof(struct block_node));
			
			blocknode->devid =ip->devid+1;
			blocknode->fileoff=off;
			blocknode->size=size;
			blocknode->addr= writeoff;
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),blocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}else{//not NULL
			//userfs_info("inode %lu,0blocknode->size %d\n",ip,blocknode->size);
			blocknode->size +=size;
		}
	}else{
		while(writesize >0){
			
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			struct block_node *blocknode =block_search(&(ip->blktree),fileoff);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			if(!blocknode){ //NULL
				blocknode=(struct block_node *)malloc(sizeof(struct block_node));
				//blocknode = block_search(&(ip->blktree),_off);
				blocknode->devid =ip->devid+1;
				blocknode->fileoff=fileoff;
				if(writesize>g_block_size_bytes){
					blocknode->size=g_block_size_bytes;
					writesize-=g_block_size_bytes;
				}else{
					blocknode->size=writesize;
					writesize=0;
				}
				
				blocknode->addr= writeoff;
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				block_insert(&(ip->blktree),blocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
			}else{//not NULL
				
				blocknode->devid =ip->devid+1;
				blocknode->fileoff=fileoff;
				if((blocknode->size+writesize)>g_block_size_bytes){
					blocknode->size=g_block_size_bytes;
					writesize-=(g_block_size_bytes-blocknode->size);
				}else{
					blocknode->size=blocknode->size +writesize;
					writesize=0;
				}
				blocknode->addr= writeoff;
				
			}
			writeoff++;
			fileoff+=g_block_size_bytes;
			
		}
	}
	//userfs_info("ssd write success %d\n",size);
	return size;
}


int fs_smallwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_info("fs_smallwrite size %d\n",size);
	struct buffer_head *bh;

	struct fuckfs_extent * fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));
	struct timeval curtime;

	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	offset_t off_aligned_2M = off &(~(((uint64_t)0x1<<g_lblock_size_shift) -1));
	
	//uint32_t size_4K =size + off - off_aligned_4K;
	uint32_t size_2M =size + off - off_aligned_2M;

	bmap_req_t bmap_req;
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode =block_search(&(ip->blktree),off_aligned_4K);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	
	if(!blocknode){ //4K NULL
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		blocknode =block_search(&(ip->blktree),off_aligned_2M);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		if(!blocknode ||(blocknode->size+blocknode->fileoff)< off){ //2M NULL
		
			bmap_req.start_offset = off_aligned_4K;
			int ret=bmap(ip,&bmap_req);
			
			userfs_info("bmap_req.start_offset %d\n",bmap_req.start_offset);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			
		}else{ //2M not NULL
			bmap_req.block_no = blocknode->addr;
			bmap_req.dev = blocknode->devid;
			bmap_req.blk_count_found = blocknode->size >>g_block_size_shift;
			userfs_info("bmap_req.block_no %d\n",bmap_req.block_no);
		}
		
	}else{ //4K not NULL
		bmap_req.block_no = blocknode->addr;
		bmap_req.dev = blocknode->devid;
		bmap_req.blk_count_found=blocknode->size >>g_block_size_shift;
		userfs_info("bmap_req.block_no %d\n",bmap_req.block_no);

	}

	addr_t blockno;
	if(!blocknode){
		userfs_info("smallwrite erorr off %d, size %d\n",off, size);
		panic("smallwrite erorr\n");
	}
	if(bmap_req.dev>1 && blocknode->size>=size_2M){
		blockno = blocknode->addr + ((off-off_aligned_2M) >>g_block_size_shift);
	}else{
		blockno = blocknode->addr;
	}

	bh = bh_get_sync_IO(blocknode->devid,blockno,BH_NO_DATA_ALLOC);
	bh->b_data=buf;
	bh->b_size=size;

	bh->b_offset=off&((1<<g_block_size_shift) -1);
	//userfs_info("blockno %d, off %d\n",blockno, bh->b_offset);

	userfs_write(bh);

	bh_release(bh);

	fsextent->device_id = blocknode->devid-1;

	offset_t writeoff =(uint64_t)bh->b_offset;
	writeoff += blockno<<g_block_size_shift;
	fsextent->ino = ip->inum;
	fsextent->ext_start_hi = (writeoff)>>32; //字節地址
	fsextent->ext_start_lo = (writeoff);
	fsextent->ext_block = off >>g_block_size_shift;

	fsextent->ext_len = size + (((uint32_t) 0x1<<g_block_size_shift) -1) >>g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	
	if((off+size)>ip->size && blocknode->devid ==1){
		ip->nvmoff += size;
		fsextent->blkoff_hi = (ip->nvmoff)>>32;
		fsextent->blkoff_lo = (ip->nvmoff);
	}else if((off+size)>ip->size && blocknode->devid > 1){
		ip->ssdoff += size;
		fsextent->blkoff_hi = (ip->ssdoff)>>32;
		fsextent->blkoff_lo = (ip->ssdoff);
	}else{
		fsextent->blkoff_hi = 0;
		fsextent->blkoff_lo = 0;
	}
	
	append_entry_log(fsextent,0);
	blocknode->size = off + size - blocknode->fileoff;
	return size;
}


int append_fsextent(struct inode *ip, offset_t off, struct buffer_head * bh, uint8_t append){
	//printf("append_fsextent ip->num %d, off %d\n", ip->inum,off);
	struct fuckfs_extent * fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));
	struct timeval curtime;
	uint8_t devid = bh->b_dev;
	fsextent->ino = ip->inum;
	fsextent->device_id = devid-1;
	fsextent->ext_start_hi = (bh->b_blocknr<<g_block_size_shift)>>32; //字節地址
	fsextent->ext_start_lo = (bh->b_blocknr<<g_block_size_shift);
	
	fsextent->ext_start_lo += off &(((uint64_t)0x1<<g_block_size_shift) -1);
	
	fsextent->ext_block = off >>g_block_size_shift;
	
	fsextent->ext_len = bh->b_size+ (((uint32_t) 0x1<<g_block_size_shift) -1) >> g_block_size_shift;
	fsextent->size =bh->b_size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	//
	//
	if(append==1&&devid==1){
		ip->nvmoff=ip->ssdoff;
		fsextent->blkoff_hi = (ip->nvmoff)>>32;
		fsextent->blkoff_lo = (ip->nvmoff);
	}else if (append==1&&devid>1)
	{
		fsextent->blkoff_hi = (ip->ssdoff)>>32;
		fsextent->blkoff_lo = (ip->ssdoff);
	}else{
		fsextent->blkoff_hi = 0;
		fsextent->blkoff_lo = 0;
	}
	append_entry_log(fsextent,0);
	return 1;
}

int append_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size, uint8_t devid, addr_t blockno){
	//userfs_info("append_write size %d, devid %d, off %d, blockno %d\n",size,devid,off,blockno);
	struct buffer_head *bh;
	uint64_t writeoff,fileoff;
	uint32_t writesize = size;
	
	struct timeval curtime;
	if(devid ==1){ //nvm地址
	
		if((off+size) <= (FILENVMBLOCK<<g_block_size_shift)){
			bh = bh_get_sync_IO(1,ip->nvmoff>>g_block_size_shift,BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;
			bh->b_offset = off&((1<<g_block_size_shift) -1);
			userfs_write(bh);
			
			fileoff=off;
			writeoff = ip->nvmoff>>g_block_size_shift;
			ip->nvmoff +=size;
			append_fsextent(ip,off,bh,1);

			bh_release(bh);
			while(writesize >0){
				struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));
				blocknode->devid =1;
				blocknode->fileoff=fileoff;
				if(writesize>g_block_size_bytes){
					blocknode->size=g_block_size_bytes;
					writesize-=g_block_size_bytes;
				}else{
					blocknode->size=writesize;
					writesize=0;
				}
				blocknode->addr= writeoff;
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				block_insert(&(ip->blktree),blocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
				writeoff++;
				fileoff+=g_block_size_bytes;
			}
		}else{
			ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
			ip->ssdoff+=off&((1<<g_lblock_size_shift) -1);
			bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;
			bh->b_offset=off&((1<<g_block_size_shift) -1);

			userfs_write(bh);
			

			fileoff=off;
			writeoff = ip->ssdoff>>g_block_size_shift;  //4K地址
			ip->ssdoff +=size;
			append_fsextent(ip,off,bh,1);
			bh_release(bh);
			while(writesize >0){
				struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));
				blocknode->devid =ip->devid+1;
				blocknode->fileoff=fileoff;
				if(writesize>g_block_size_bytes){
					blocknode->size=g_block_size_bytes;
					writesize-=g_block_size_bytes;
				}else{
					blocknode->size=writesize;
					writesize=0;
				}
				blocknode->addr= writeoff;

				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				block_insert(&(ip->blktree),blocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
				writeoff++;
				fileoff+=g_block_size_bytes;
			}
		}
	}else{

		bh = bh_get_sync_IO(devid,blockno+1,BH_NO_DATA_ALLOC);
		bh->b_data=buf;
		bh->b_size=size;
		bh->b_offset=off&((1<<g_block_size_shift) -1);
		userfs_write(bh);


		fileoff=off;
		writeoff = blockno+1;  //4K地址
		ip->ssdoff =(blockno+1)>>g_block_size_shift+size;
		append_fsextent(ip,off,bh,1);
		bh_release(bh);
		while(writesize >0){
			struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));
			blocknode->devid =ip->devid+1;
			blocknode->fileoff=fileoff;
			if(writesize>g_block_size_bytes){
				blocknode->size=g_block_size_bytes;
				writesize-=g_block_size_bytes;
			}else{
				blocknode->size=writesize;
				writesize=0;
			}
			blocknode->addr= writeoff;
			//userfs_info("inode %lu, newblocknode->size %d\n",ip,blocknode->size);
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),blocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			writeoff++;
			fileoff+=g_block_size_bytes;
		}

	}
	return size;
}

//追加写和in-place update
int aligned_2M_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_info("aligned_2M_write size %d, off %d, ip->size %d, ip->ssdoff %ld\n",size,off,ip->size,ip->ssdoff);
	int retsize;
	if(off==ip->size){ //append write, 而且2M对齐
		if((off+size) <= (FILENVMBLOCK<<g_block_size_shift) || ((ip->nvmblock ==0) &&(FILENVMBLOCK >=(size>>g_block_size_shift)))){
			retsize=fs_nvmwrite(ip, buf, off, size);
			if(retsize==-1 && (ip->ssdoff<<43)==0){
				retsize = fs_ssdwrite(ip, buf, off, size);
			}
			if(retsize==-1){
				panic("write exist bug\n");
			}
		}else{
			if((ip->ssdoff<<43)==0){
				retsize=fs_ssdwrite(ip, buf, off, size);
			}else{
				userfs_info("write exits error ip->ssdoff %d\n",ip->ssdoff);
				panic("write exits error\n");
			}
		}
		
		return retsize;
	}else{ //in-place update &/ append write
		
		struct fuckfs_extent * fsextent;
		struct timeval curtime;

		uint32_t remainsize = size,onewsize,writesize=0;
		bmap_req_t bmap_req;
		struct block_node *blocknode;
		struct buffer_head *bh;
		offset_t fileoff = off;
		uint8_t devid;
		addr_t blockno;
		int addsize=0;
		uint8_t append=0;
		
		
		while(remainsize > 0){
			fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),fileoff); //off对应的数据地址
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			if(!blocknode){//NULL
				bmap_req.start_offset=fileoff;
				int ret=bmap(ip,&bmap_req);
				//userfs_info("bmap ret %d\n",ret);
				pthread_rwlock_rdlock(&ip->blktree_rwlock);
				blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
			}
			if(blocknode&&blocknode->devid>1 && fileoff==off){ //2M write
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
				bh->b_data=buf;
				bh->b_size=size;
				bh->b_offset=0;
				userfs_write(bh);
				
				writesize =size;
				addsize = fileoff+size - ip->size;
				if(addsize>0){
					blocknode->size += addsize;
					ip->ssdoff += addsize;
					append=1;
				}
				append_fsextent(ip,fileoff,bh,append);
				bh_release(bh);
				
				break;
			}
			if(blocknode){   //4K write
				append=0;
				onewsize = remainsize< blocknode->size? remainsize:blocknode->size;
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
				bh->b_data=buf+writesize;
				bh->b_size=onewsize;

				bh->b_offset=0;
				userfs_write(bh);
				//bh_release(bh);

				devid =blocknode->devid;
				blockno=blocknode->addr;
				addsize = onewsize - blocknode->size;
				if(addsize>0){
					blocknode->size=onewsize;
					if(devid==1){
						ip->nvmoff+=addsize;
					}else{
						ip->ssdoff+=addsize;
					}
					append=1;
				}
				//userfs_info("onewsize %d, writesize %d\n",onewsize,writesize);
				append_fsextent(ip,fileoff,bh,append);
				bh_release(bh);
				writesize+=onewsize;
				fileoff+=onewsize;
				remainsize-=onewsize;
				
				
			}else{
				
				userfs_info("bug append_write writesize %d\n",writesize);
				//retsize = append_write(ip,buf+writesize, fileoff,remainsize,devid,blockno);
				return size;
				writesize+=retsize;
			}
		}



		return writesize;
	}
}

int in_place_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_info("in_place_write size %d\n",size);
	uint32_t remainsize = size,onewsize,writesize=0;
	int addsize=0;
	bmap_req_t bmap_req;
	struct block_node *blocknode;
	struct buffer_head *bh;
	offset_t off_aligned_2M = off &(~(((uint64_t)0x1<<g_lblock_size_shift) -1));
	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	uint32_t sized =off - off_aligned_2M;
	uint8_t devid,append=0;
	while(remainsize > 0){
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		blocknode =block_search(&(ip->blktree),off);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		if(!blocknode){//NULL
			bmap_req.start_offset=off;
			int ret =bmap(ip,&bmap_req);
			//userfs_info("bmap ret %d\n",ret);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}
		if(blocknode&&blocknode->fileoff == off_aligned_2M &&blocknode->devid>1){ //在2M块中写
			//userfs_info("2M blocknode->fileoff %d\n",blocknode->fileoff);
			append=0;
			onewsize = remainsize< (blocknode->size - sized) ? remainsize:(blocknode->size - sized);
			bh=bh_get_sync_IO(blocknode->devid,blocknode->addr +((off-off_aligned_2M)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			bh->b_data=buf+writesize;
			bh->b_size=onewsize;

			bh->b_offset=0;
			userfs_write(bh);
			//bh_release(bh);
			
			addsize = sized - blocknode->size;
			devid = blocknode->devid;
			if(addsize>0){
				blocknode->size = sized;
				if(devid==1){
					ip->nvmoff+=addsize;
				}else{
					ip->ssdoff+=addsize;
				}
				append=1;
			}
			append_fsextent(ip,off,bh,append);
			bh_release(bh);

			writesize+=onewsize;
			off+=onewsize;
			remainsize-=onewsize;

		}else if(blocknode && blocknode->fileoff == off_aligned_4K){ //4K写
			//userfs_info("4k blocknode->fileoff %d\n",blocknode->fileoff);
			append=0;
			onewsize = remainsize< blocknode->size ? remainsize:blocknode->size;
			bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
			bh->b_data=buf+writesize;
			bh->b_size=onewsize;

			bh->b_offset=0;
			userfs_write(bh);
			//bh_release(bh);
			
			sized= (off-off_aligned_4K);
			addsize = sized - blocknode->size;
			devid = blocknode->devid;
			if(addsize>0){
				blocknode->size = sized;
				if(devid==1){
					ip->nvmoff+=addsize;
				}else{
					ip->ssdoff+=addsize;
				}
				append=1;
			}
			append_fsextent(ip,off,bh,append);
			bh_release(bh);

			writesize+=onewsize;
			off+=onewsize;
			remainsize-=onewsize;
			//userfs_info("remainsize %d\n",remainsize);
		}else{
			//userfs_info("retsize %d\n",writesize);
			return writesize;
		}
	}
	return writesize;
}



int aligned_4K_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_info("aligned_4K_write size %d, off %d, ip->size %d\n",size, off,ip->size);
	int retsize;
	offset_t off_aligned_2M = off &(~(((uint64_t)0x1<<g_lblock_size_shift) -1));
	struct block_node *blocknode;// =block_search(&(ip->blktree),off_aligned_2M);
	struct buffer_head *bh;
	bmap_req_t bmap_req;
	if(off==ip->size){ //append write
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		blocknode =block_search(&(ip->blktree),off_aligned_2M);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		if(!blocknode){ //寻找2M位置的地址
			bmap_req.start_offset=off_aligned_2M;
			int ret=bmap(ip,&bmap_req);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}
		if(blocknode->devid>1){//如果是ssd地址
			bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off-off_aligned_2M)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;

			bh->b_offset=0;
			userfs_write(bh);
	
			if(blocknode->size <(off+size-off_aligned_2M)){
				blocknode->size =(off+size-off_aligned_2M);
			}
			ip->ssdoff +=size;
			append_fsextent(ip,off,bh,1);
			bh_release(bh);
			return size;
		}else{//没有2M的SSD地址
			retsize= in_place_write(ip, buf, off, size);
			//userfs_info("retsize %d, size %d\n",retsize,size);
			if(retsize < size){
				size = size - retsize;
				offset_t off_aligned_4K_pre = off  +retsize - g_block_size_bytes;
				pthread_rwlock_rdlock(&ip->blktree_rwlock);
				blocknode =block_search(&(ip->blktree),off_aligned_4K_pre);//寻找前面块的地址
				pthread_rwlock_unlock(&ip->blktree_rwlock);
				if(!blocknode){ 
					bmap_req.start_offset=off_aligned_4K_pre;
					int ret=bmap(ip,&bmap_req);
				
					pthread_rwlock_rdlock(&ip->blktree_rwlock);
					blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
					pthread_rwlock_unlock(&ip->blktree_rwlock);
				}
				if(blocknode!=NULL&&blocknode->devid >1){//ssd地址
					bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+1,BH_NO_DATA_ALLOC);
					bh->b_data=buf;
					bh->b_size=size;

					bh->b_offset=0;
					userfs_write(bh);
					//bh_release(bh);
					blocknode->size+=size;
					ip->ssdoff +=size;
					append_fsextent(ip,off,bh,1);
					bh_release(bh);
				}else{//nvm 地址，需要判断分配nvm地址还是ssd地址

					uint32_t writesize=size;
					unsigned long writeoff, fileoff;

					//if(ip->nvmoff == off && (off+size) <= (FILENVMBLOCK<<g_block_size_shift)){
					if((off+size) <= (FILENVMBLOCK<<g_block_size_shift)){
						bh = bh_get_sync_IO(1,ip->nvmoff>>g_block_size_shift,BH_NO_DATA_ALLOC);
						bh->b_data=buf;
						bh->b_size=size;
						bh->b_offset = off&((1<<g_block_size_shift) -1);
						userfs_write(bh);
						//bh_release(bh);
						fileoff=off;
						writeoff = ip->nvmoff>>g_block_size_shift;
						ip->nvmoff +=size;
						append_fsextent(ip,off,bh,1);
						bh_release(bh);
						while(writesize >0){
							struct block_node *newblocknode =(struct block_node *)malloc(sizeof(struct block_node));
							newblocknode->devid =1;
							newblocknode->fileoff=fileoff;
							if(writesize>g_block_size_bytes){
								newblocknode->size=g_block_size_bytes;
								writesize-=g_block_size_bytes;
							}else{
								newblocknode->size=writesize;
								writesize=0;
							}
							newblocknode->addr= writeoff;
							//userfs_info("inode %lu, newblocknode->size %d, newblocknode->fileoff %d,  newblocknode->addr %ld\n",ip,newblocknode->size,newblocknode->fileoff,newblocknode->addr);
							//userfs_info("inode %lu, newblocknode->size %d\n",ip,newblocknode->size);
							pthread_rwlock_wrlock(&ip->blktree_rwlock);
							block_insert(&(ip->blktree),newblocknode);
							pthread_rwlock_unlock(&ip->blktree_rwlock);
							writeoff++;
							fileoff+=g_block_size_bytes;
						}

					}else{
						ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
						ip->ssdoff+=off&((1<<g_lblock_size_shift) -1);
						bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
						bh->b_data=buf;
						bh->b_size=size;
						bh->b_offset=off&((1<<g_block_size_shift) -1);
						//printf("ssdwrite devid %d ssdoff %d, bh->b_offset %d\n", ip->devid, ip->ssdoff>>12,bh->b_offset);
						userfs_write(bh);
						fileoff=off;
						writeoff = ip->ssdoff>>g_block_size_shift;  //4K地址
						ip->ssdoff +=size;
						append_fsextent(ip,off,bh,1);
						bh_release(bh);
						while(writesize >0){
							struct block_node *newblocknode =(struct block_node *)malloc(sizeof(struct block_node));
							newblocknode->devid =ip->devid+1;
							newblocknode->fileoff=fileoff;
							if(writesize>g_block_size_bytes){
								newblocknode->size=g_block_size_bytes;
								writesize-=g_block_size_bytes;
							}else{
								newblocknode->size=writesize;
								writesize=0;
							}
							newblocknode->addr= writeoff;
							//userfs_info("inode %lu, newblocknode->size %d, newblocknode->fileoff %d,  newblocknode->addr %ld\n",ip,newblocknode->size,newblocknode->fileoff,newblocknode->addr);
							pthread_rwlock_wrlock(&ip->blktree_rwlock);
							block_insert(&(ip->blktree),newblocknode);
							pthread_rwlock_unlock(&ip->blktree_rwlock);
							writeoff++;
							fileoff+=g_block_size_bytes;
						}
					}
				}
			}else{
				return retsize;
			}
			retsize+=size;
			//userfs_info("retsize %d\n",retsize);
			return retsize;
		}
	}else{ //in-place update

		uint32_t ipwsize=in_place_write(ip, buf, off, size);
		//userfs_info("ipwsize %d, size %d\n",ipwsize,size);
		if(ipwsize<size){
			unsigned long fileoff = off+ipwsize;
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode = block_search(&(ip->blktree),fileoff- g_block_size_bytes);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			uint32_t apwsize=append_write(ip,buf+ipwsize,off+ipwsize,size-ipwsize,blocknode->devid,blocknode->addr);
			if(ipwsize+apwsize == size){
				return size;
			}else{
				panic("write exist bug\n");
			}
		}else{
			return ipwsize;
		}
	}
	return retsize;
}

//原地更新写到nvm
//原地更新时需要确保blktree索引数据正确
//append更新写到ssd
//地址如何控制，nvmoff 和SSDoff
//如果前部分写到nvm中，后续append如何写到ssd中

int upsert_blktree(struct inode *ip, uint8_t devid, unsigned long addr, offset_t off, uint32_t size){
	userfs_info("upsert_blktree off %d,size %d\n",off, size);
	int ret;
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode =block_search(&(ip->blktree),off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	//userfs_info("inode %lu, blocknode %lu\n", ip,blocknode);
	if(!blocknode){ //NULL
		blocknode =(struct block_node *)malloc(sizeof(struct block_node));
		blocknode->devid =devid+1;
		blocknode->fileoff=off;
		blocknode->size=size;

		blocknode->addr= addr >>g_block_size_shift;
		//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		block_insert(&(ip->blktree),blocknode);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		return 1;		
	}else{
		//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
		offset_t bnodeoff= blocknode->size +blocknode->fileoff;
		offset_t writeoff= size+off;
		if(bnodeoff>writeoff){ //在其中
			struct block_node *afblocknode =(struct block_node *)malloc(sizeof(struct block_node));
			afblocknode->devid =blocknode->devid;
			afblocknode->fileoff=ALIGN(writeoff, g_block_size_bytes);
			afblocknode->size=bnodeoff - afblocknode->fileoff;

			afblocknode->addr=  blocknode->addr + ((afblocknode->fileoff - blocknode->fileoff)>>g_block_size_shift);
			//userfs_info("inode %lu, afblocknode->size %d, afblocknode->fileoff %d,  afblocknode->addr %ld\n",ip,afblocknode->size,afblocknode->fileoff,afblocknode->addr);
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),afblocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);

			
			blocknode->size = off - blocknode->fileoff;
			if(blocknode->size ==0){
				blocknode->size = size;
				blocknode->addr = addr>>g_block_size_shift;
				blocknode->devid =devid;
			}else{
				struct block_node *inblocknode =(struct block_node *)malloc(sizeof(struct block_node));
				inblocknode->devid =devid;
				inblocknode->fileoff=off;
				inblocknode->size=size;

				inblocknode->addr= addr>>g_block_size_shift;
				//userfs_info("inode %lu, inblocknode->size %d, inblocknode->fileoff %d,  inblocknode->addr %ld\n",ip,inblocknode->size,inblocknode->fileoff,inblocknode->addr);
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				ret = block_insert(&(ip->blktree),inblocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
			}

				
			block_search(&(ip->blktree),blocknode->fileoff);
		}else{
			
			blocknode->size = off - blocknode->fileoff;
			if(blocknode->size ==0){
				blocknode->size = size;
				blocknode->addr = addr>>g_block_size_shift;
				blocknode->devid =devid;
			}else{
				struct block_node *inblocknode =(struct block_node *)malloc(sizeof(struct block_node));
				inblocknode->devid =devid;
				inblocknode->fileoff=off;
				inblocknode->size=size;

				inblocknode->addr= addr>>g_block_size_shift;
				//userfs_info("inode %lu, inblocknode->size %d, inblocknode->fileoff %d,  inblocknode->addr %ld\n",ip,inblocknode->size,inblocknode->fileoff,inblocknode->addr);
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				ret=block_insert(&(ip->blktree),inblocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
				
			}
			block_search(&(ip->blktree),blocknode->fileoff);
			
		}
	}

	return 1;
}

int insert_blktree(struct inode *ip, uint8_t devid, unsigned long addr, offset_t off, uint32_t size){
	//userfs_info("inode %lu, addr %ld, off %d, size %d");
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode =block_search(&(ip->blktree),off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	if(!blocknode){
		struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));
		blocknode->devid =devid;
		blocknode->fileoff=off;
		blocknode->size=size;

		blocknode->addr= addr>>g_block_size_shift;
		//userfs_info("inode %lu, devid %d, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,devid,blocknode->size,blocknode->fileoff,blocknode->addr);
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		block_insert(&(ip->blktree),blocknode);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
	}else{
		blocknode->devid =devid;
		blocknode->fileoff=off;
		blocknode->size=size;

		blocknode->addr= addr>>g_block_size_shift;
		//userfs_info("inode %lu, devid %d, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,devid,blocknode->size,blocknode->fileoff,blocknode->addr);

	}
	return 1;		
}

int upsert_blktree_bh(struct inode *ip, offset_t off, struct buffer_head * bh){
	uint8_t devid = bh->b_dev;
	unsigned long addr = bh->b_blocknr;
	uint32_t size = bh->b_size;
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode =block_search(&(ip->blktree),off);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	//userfs_info("inode %lu, blocknode %lu\n", ip,blocknode);
	if(!blocknode){ //NULL
		blocknode =(struct block_node *)malloc(sizeof(struct block_node));
		blocknode->devid =devid;
		blocknode->fileoff=off;
		blocknode->size=size;

		blocknode->addr= addr;
		//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		block_insert(&(ip->blktree),blocknode);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		return 1;		
	}else{
		offset_t bnodeoff= blocknode->size +blocknode->fileoff;
		offset_t writeoff= size+off;
		if(bnodeoff>writeoff){
			struct block_node *afblocknode =(struct block_node *)malloc(sizeof(struct block_node));
			afblocknode->devid =blocknode->devid;
			afblocknode->fileoff=ALIGN(writeoff, g_block_size_bytes);
			afblocknode->size=bnodeoff - afblocknode->fileoff;

			afblocknode->addr=  blocknode->addr + ((afblocknode->fileoff - blocknode->fileoff)>>g_block_size_shift);
			//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),afblocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);

			struct block_node *inblocknode =(struct block_node *)malloc(sizeof(struct block_node));
			inblocknode->devid =devid;
			inblocknode->fileoff=off;
			inblocknode->size=size;

			inblocknode->addr= addr;
			//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),inblocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);

			blocknode->size = off - blocknode->fileoff;

		}else{
			struct block_node *inblocknode =(struct block_node *)malloc(sizeof(struct block_node));
			inblocknode->devid =devid;
			inblocknode->fileoff=off;
			inblocknode->size=size;

			inblocknode->addr= addr;
			//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),inblocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);

			blocknode->size = off - blocknode->fileoff;
		}
	}

	return 1;
}

int insert_blktree_bh(struct inode *ip, offset_t off, struct buffer_head * bh){
	//printf("insert_blktree_bh inum %d, off %d\n",ip->inum,off);
	struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));
	blocknode->devid =bh->b_dev;
	blocknode->fileoff=off;
	blocknode->size=bh->b_size;

	blocknode->addr= bh->b_blocknr;
	userfs_info("inode %lu, devid %d, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,bh->b_dev,blocknode->size,blocknode->fileoff,blocknode->addr);
	pthread_rwlock_wrlock(&ip->blktree_rwlock);
	block_insert(&(ip->blktree),blocknode);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	return 1;		
}


typedef struct{
    void * func_arg;
    uint8_t wflag;
    struct inode *inode;
    offset_t off;
    uint8_t append;
}wargument;

int unaligned_smallwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){   //小于4K非对齐写
	//userfs_printf("unaligned_smallwrite off %d, write size %d, filesize %d\n",off,size,ip->size);
	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	struct buffer_head *bh;
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode = block_search(&(ip->blktree),off_aligned_4K);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	if(!blocknode){ //NULL
		bmap_req_t bmap_req;
		bmap_req.start_offset=off_aligned_4K;
		int ret=bmap(ip,&bmap_req);
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
	}
	if(blocknode){
		if(off==ip->size){   //append写,直接在后面写，追加size到blocknode->size
			bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((blocknode->fileoff-off_aligned_4K)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;
			bh->b_offset=off-off_aligned_4K;
			ip->ssdoff += size;
#ifndef CONCURRENT	
			userfs_write(bh);
			append_fsextent(ip,off,bh,1);
			bh_release(bh);
			
#else
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = bh;
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =2;
    		int ret = thpool_add_work(writepool[bh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",bh->b_dev,threadpool_queuesize(writepool[bh->b_dev]));
				panic("cannot add thread\n");
			}
		
#endif
			blocknode->size += size;
			
		}else if(off<ip->size){   //in-place 更新
			char *readbuf = malloc(g_block_size_bytes);
			struct buffer_head *readbh;
			unsigned long addr;
			
			if(((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||((ip->rwoff-ip->rwsoff)>>g_block_size_shift)>=512))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					
					case 1: 
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					case 2: 
						ip->rwoff = allocate_ssd2block();
						ip->devid=2;
						break;
					case 3:
						ip->rwoff = allocate_ssd3block();
						ip->devid=3;
						break;
						
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->rwoff = ip->nvmoff;
						ip->rwnvmsize = ip->nvmoff;
						//ip->rwnvmsize += size>>g_block_size_shift;
						ip->devid=0;
						break;
						
					/*
					default:
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					*/
				}
				ip->rwsoff = ip->rwoff;
				//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->rwoff %ld, ip->wrthdnum %d, ip->rwsoff %lu\n",ip->inum,ip->devid, ip->rwoff,ip->wrthdnum,ip->rwsoff);
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}

			bh = bh_get_sync_IO(1,ip->rwoff>>12,BH_NO_DATA_ALLOC);  

			readbh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off_aligned_4K-blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			readbh->b_size=4096;
			readbh->b_offset =0;
			readbh->b_data = readbuf;
			bh_release(readbh);
			memcpy(readbuf+(off-off_aligned_4K),buf,size);
			bh->b_data=readbuf;
			bh->b_size=g_block_size_bytes;
			bh->b_offset=0;
			addr = ip->rwoff >> g_block_size_shift;
			ip->rwoff +=g_block_size_bytes;
#ifndef CONCURRENT			
			userfs_write(bh);
			
			append_fsextent(ip,off,bh,0); 
			bh_release(bh);
		
#else
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = bh;
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =0;
    		int ret = thpool_add_work(writepool[bh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",bh->b_dev,threadpool_queuesize(writepool[bh->b_dev]));
				panic("cannot add thread\n");
			}
		
#endif
			
			//insert to blocktree
		}else{
			userfs_printf("write not supported offset %d, ip->size %d\n",off,ip->size);
			panic("write not supported\n");
		}
	}else{
		panic("unaligned_smallwrite bug!!");
	}
	return size;

}

int aligned_smallwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_printf("aligned_smallwrite inode %lu, off %d, file size %d, write size %d\n",ip,off,ip->size,size);
	struct buffer_head *bh;
	unsigned long addr;

	
	if(off<ip->size){   //in-place 更新
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		struct block_node *blocknode = block_search(&(ip->blktree),off);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		//userfs_info("blocknode %lu, off %d, ip->blktree %lu\n",blocknode,off,ip->blktree);
		if(!blocknode){ //NULL
			bmap_req_t bmap_req;
			bmap_req.start_offset=off;
			int ret=bmap(ip,&bmap_req);
			//userfs_info("bmap ret %d\n",ret);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}
		//userfs_info("blocknode fileoff %d, addr %ld\n", blocknode->fileoff,blocknode->addr);
		if(blocknode &&((blocknode->fileoff+blocknode->size)>(off+size))){ //原有数据完全覆盖写的数据，读取此块后半部分数据，和要写的数据一起写入新的nvm块
			char *readbuf = malloc(g_block_size_bytes);
			struct buffer_head *readbh;
			readbh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off-blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			uint32_t readsize = blocknode->fileoff+blocknode->size - off;
			if(readsize >=g_block_size_shift){
				readsize = g_block_size_bytes;
			}
			readbh->b_size = readsize;
			readbh->b_offset = 0;
			readbh->b_data = readbuf;
			
			memcpy(readbuf,buf,size);  //4K块的前部分(buf)cp到readbuf

			
			if(((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||((ip->rwoff-ip->rwsoff)>>g_block_size_shift)>=512))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					case 1: 
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					case 2: 
						ip->rwoff = allocate_ssd2block();
						ip->devid=2;
						break;
					case 3:
						ip->rwoff = allocate_ssd3block();
						ip->devid=3;
						break;
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->rwoff = ip->nvmoff;
						ip->rwnvmsize = ip->nvmoff;
						//ip->rwnvmsize += size>>g_block_size_shift;
						ip->devid=0;
						break;
					/*
					default:
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					*/
				}
				ip->rwsoff = ip->rwoff;
				//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->rwoff %ld, ip->wrthdnum %d, ip->rwsoff %lu\n",ip->inum,ip->devid, ip->rwoff,ip->wrthdnum,ip->rwsoff);
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}
			bh = bh_get_sync_IO(1,ip->rwoff>>12,BH_NO_DATA_ALLOC);
			bh->b_data=readbuf;
			bh->b_size=readsize;
			bh->b_offset=0;
			addr = ip->rwoff;
			ip->rwoff += g_block_size_bytes;
#ifndef CONCURRENT
			userfs_write(bh);
			
			append_fsextent(ip,off,bh,0);
			bh_release(bh);
			//upsert_blktree(ip,1, addr, off, readsize);
#else
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = bh;
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =0;
    		int ret = thpool_add_work(writepool[bh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",bh->b_dev,threadpool_queuesize(writepool[bh->b_dev]));
				panic("cannot add thread\n");
			}
#endif
			

		}else if((blocknode->fileoff+blocknode->size)>(off+size)){   //块中数据小于写的数据
			//ip->nvmoff=allocate_nvmblock(1);
			//userfs_info("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, (ip->rwoff-ip->rwsoff)>>g_block_size_shift, ip->rwsoff);
			if(((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||((ip->rwoff-ip->rwsoff)>>g_block_size_shift)>=512))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					case 1: 
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					case 2: 
						ip->rwoff = allocate_ssd2block();
						ip->devid=2;
						break;
					case 3:
						ip->rwoff = allocate_ssd3block();
						ip->devid=3;
						break;
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->rwoff = ip->nvmoff;
						ip->rwnvmsize = ip->nvmoff;
						//ip->rwnvmsize += size>>g_block_size_shift;
						ip->devid=0;
						break;
					
					/*
					default:
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					*/
				}
				ip->rwsoff = ip->rwoff;
				//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->rwoff %ld, ip->wrthdnum %d, ip->rwsoff %lu\n",ip->inum,ip->devid, ip->rwoff,ip->wrthdnum,ip->rwsoff);
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}

			bh = bh_get_sync_IO(1,ip->rwoff>>12,BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;
			bh->b_offset=0;
			addr = ip->rwoff;
			ip->rwoff += g_block_size_bytes;
//bug
#ifndef CONCURRENT			
			userfs_write(bh);
			
			append_fsextent(ip,off,bh,1); 
			bh_release(bh);
			
			insert_blktree(ip,1, addr, off, size);
#else
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = bh;
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =1;
    		int ret = thpool_add_work(writepool[bh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",bh->b_dev,threadpool_queuesize(writepool[bh->b_dev]));
				panic("cannot add thread\n");
			}
			
#endif
	
		}
		return size;
	}
	

	if(((ip->ssdoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->ssdoff ==0)||((ip->ssdoff-ip->usednvms)>>g_block_size_shift)>=512))){
		ip->wrthdnum = ip->wrthdnum % writethdnum;
		//userfs_info("ip->ino %d, ip->devid %d, ip->ssdoff %d, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
		switch(ip->wrthdnum){
			case 1: 
				ip->ssdoff = allocate_ssd1block();
				ip->devid=1;
				break;
			case 2: 
				ip->ssdoff = allocate_ssd2block();
				ip->devid=2;
				break;
			case 3:
				ip->ssdoff = allocate_ssd3block();
				ip->devid=3;
				break;
			default:
				ip->nvmoff = allocate_nvmblock(512);
				ip->ssdoff = ip->nvmoff;
				ip->usednvms = ip->nvmoff;
				ip->devid=0;
				break;
			
			/*
				default:
					ip->nvmoff = allocate_nvmblock(512);
					ip->ssdoff = ip->nvmoff;
					ip->usednvms = ip->nvmoff ;
					ip->devid=0;
					userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
					break;
				*/
				/*
				default:
					ip->ssdoff = allocate_ssd1block();
					ip->devid=1;
					break;
				*/
		}

		__sync_fetch_and_add(&ip->wrthdnum,1);
	}

	offset_t off_in_2M = off &(((uint64_t)0x1<<g_lblock_size_shift) -1);
	offset_t writeoff = ip->ssdoff &(((uint64_t)0x1<<g_lblock_size_shift) -1);
	if(ip->devid!=0 &&  writeoff!=off_in_2M){
		userfs_printf("write addr error ip->ssdoff %ld, off_in_2M %d, writeoff %d\n",ip->ssdoff,off_in_2M,writeoff);
		panic("write addr error\n");
	}
	bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
	bh->b_data=buf;
	bh->b_size=size;
	bh->b_offset=0;
	addr = ip->ssdoff;
	ip->ssdoff +=size;
#ifndef CONCURRENT		
	userfs_write(bh);
	
	append_fsextent(ip,off,bh,1);
	bh_release(bh);
	insert_blktree(ip,ip->devid+1, addr, off, size);
#else
	wargument *warg = (wargument *)malloc(sizeof(wargument));
    warg->func_arg = bh;
    warg->wflag = 1;
    warg->inode = ip;
    warg->off = off;
    warg->append =1;
    int ret = thpool_add_work(writepool[bh->b_dev], userfs_write, (void *)warg);
	if(ret < 0){
		printf("writepool[%d] queue_size %d\n",bh->b_dev,threadpool_queuesize(writepool[bh->b_dev]));
		panic("cannot add thread\n");
	}
#endif
	
	
	
	//insert to blocktree
	return size;
}



int aligned_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	userfs_printf("aligned write filesize %d, off %d, size %d\n",ip->size,off,size);
	//printf("writethdnum %d\n", writethdnum);
	uint64_t writesize=0;
	struct buffer_head *ibh,*abh;
	unsigned long addr;

	if(off == ip->size){ //append写，直接写入后面   如果前半部分写到nvm，如何安置后半部分
		if(((ip->ssdoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->ssdoff ==0)||((ip->ssdoff-ip->usednvms)>>g_block_size_shift)>=512))){
			//userfs_printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld, (ip->ssdoff-ip->usednvms)>>g_block_size_shift %d\n", ip->devid,ip->ssdoff,ip->ssdoff&0x1fffffUL,(ip->ssdoff-ip->usednvms)>>g_block_size_shift);
			ip->wrthdnum = ip->wrthdnum % writethdnum;
			
			switch(ip->wrthdnum){
				case 1: 
					ip->ssdoff = allocate_ssd1block();
					ip->devid=1;
					break;
				case 2: 
					ip->ssdoff = allocate_ssd2block();
					ip->devid=2;
					break;
				case 3:
					ip->ssdoff = allocate_ssd3block();
					ip->devid=3;
					break;
				default:
					ip->nvmoff = allocate_nvmblock(512);
					ip->ssdoff = ip->nvmoff;
					ip->usednvms = ip->nvmoff ;
					ip->devid=0;
					//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
					break;
			/*
				default:
					ip->nvmoff = allocate_nvmblock(512);
					ip->ssdoff = ip->nvmoff;
					ip->usednvms = ip->nvmoff ;
					ip->devid=0;
					//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
					break;

				default:
					ip->ssdoff = allocate_ssd1block();
					ip->devid=1;
					break;
				*/	
					
			}
			userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
			//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
			__sync_fetch_and_add(&ip->wrthdnum,1);
		}
		offset_t off_in_2M = off &(((uint64_t)0x1<<g_lblock_size_shift) -1);
		offset_t writeoff = ip->ssdoff &(((uint64_t)0x1<<g_lblock_size_shift) -1);
		
		if(writeoff!=off_in_2M){
			printf("write addr error ip->ssdoff %ld, off %d, off_in_2M %d, writeoff %d\n",ip->ssdoff,off,off_in_2M,writeoff);
			panic("write addr error\n");
		}
		//printf("write address: ip->ssdoff %ld\n",ip->ssdoff);
 		char *writebuff=malloc(size);
		memcpy(writebuff, buf, size);
		abh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
		abh->b_data=writebuff;
		abh->b_size=size;
		abh->b_offset=0;

		addr= ip->ssdoff;
		ip->ssdoff +=size;
#ifndef CONCURRENT
		//printf("direct_write\n");
		userfs_write(abh);
		
		append_fsextent(ip,off,abh,1);
		bh_release(abh);
		insert_blktree(ip,ip->devid+1, addr, off, size);
#else
		
    	wargument *warg = (wargument *)malloc(sizeof(wargument));
    	warg->func_arg = abh;
    	warg->wflag = 1;
    	warg->inode = ip;
    	warg->off = off;
    	warg->append =1;
		int ret = thpool_add_work(writepool[abh->b_dev], userfs_write, (void *)warg);
		if(ret < 0){
			printf("writepool[%d] queue_size %d\n",abh->b_dev,threadpool_queuesize(writepool[abh->b_dev]));
			panic("cannot add thread\n");
		}
#endif
		
		
#ifndef CONCURRENT
		//bh_release(abh);
#endif

	}else if(off < ip->size){   //in-place 写，但是可能后半部分为append
		userfs_info("inplace1 write off %d\n",off);
		uint32_t inplace;
		uint64_t afilesize = ALIGN(off + size, g_block_size_bytes);
		uint64_t bfilesize = ALIGN(ip->size,g_block_size_bytes);
		uint64_t delta;
		if(afilesize > bfilesize){

			inplace = bfilesize - off; //off---off+inplace为inplace， off+inplace---afilesize 为append
			//ip->nvmoff=allocate_nvmblock(inplace>>g_block_size_bytes);
			delta = (ip->rwoff-ip->rwsoff)>>g_block_size_shift;

			userfs_printf("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, delta, ip->rwsoff);

			if((delta+ (inplace>>g_block_size_shift)>512)||((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||(delta>=512)))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					case 1: 
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					case 2: 
						ip->rwoff = allocate_ssd2block();
						ip->devid=2;
						break;
					case 3:
						ip->rwoff = allocate_ssd3block();
						ip->devid=3;
						break;
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->rwoff = ip->nvmoff;
						ip->rwnvmsize = ip->nvmoff;
						//ip->rwnvmsize += size>>g_block_size_shift;
						ip->devid=0;
						break;
			
					/*
					default:
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					*/
				}
				ip->rwsoff = ip->rwoff;
				userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->rwoff %ld, ip->wrthdnum %d, ip->rwsoff %lu\n",ip->inum,ip->devid, ip->rwoff,ip->wrthdnum,ip->rwsoff);
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}

			ibh = bh_get_sync_IO(1,ip->rwoff>>12,BH_NO_DATA_ALLOC);
			ibh->b_data=buf;
			ibh->b_size=inplace;
			ibh->b_offset=0;
			
			addr= ip->rwoff;
			ip->rwoff +=inplace;
#ifndef CONCURRENT			
			userfs_write(ibh);
			
			append_fsextent(ip,off,ibh,0);
			bh_release(ibh);
			//老的索引此块的索引需不需要更新
			//upsert_blktree(ip, 1, addr, off, inplace);
#else
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = ibh;
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =0;
    		int ret = thpool_add_work(writepool[ibh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",ibh->b_dev,threadpool_queuesize(writepool[ibh->b_dev]));
				panic("cannot add thread\n");
			}

#endif

			//append 写
			if(((ip->ssdoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->ssdoff ==0)||((ip->ssdoff-ip->usednvms)>>g_block_size_shift)>=512))){
				//userfs_printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld, (ip->ssdoff-ip->usednvms)>>g_block_size_shift %d\n", ip->devid,ip->ssdoff,ip->ssdoff&0x1fffffUL,(ip->ssdoff-ip->usednvms)>>g_block_size_shift);
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					
					case 1: 
						ip->ssdoff = allocate_ssd1block();
						ip->devid=1;
						break;
					case 2: 
						ip->ssdoff = allocate_ssd2block();
						ip->devid=2;
						break;
					case 3:
						ip->ssdoff = allocate_ssd3block();
						ip->devid=3;
						break;
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->usednvms = ip->nvmoff ;
						ip->devid=0;
						//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
						break;
						
					
					/*
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->usednvms = ip->nvmoff ;
						ip->devid=0;
						userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
						break;
					*/
					/*
					default:
						ip->ssdoff = allocate_ssd1block();
						ip->devid=1;
						break;
					*/
						
				}
				
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}

			offset_t off_in_2M = off &(((uint64_t)0x1<<g_lblock_size_shift) -1);
			offset_t writeoff = ip->ssdoff &(((uint64_t)0x1<<g_lblock_size_shift) -1);
			if(writeoff!=off_in_2M){
				userfs_printf("write addr error ip->ssdoff %ld, off %d, off_in_2M %d, writeoff %d\n",ip->ssdoff,off,off_in_2M,writeoff);
				panic("write addr error\n");
			}
			abh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
			abh->b_data=buf+inplace;
			abh->b_size=size-inplace;
			abh->b_offset=0;
			addr= ip->ssdoff;
			ip->ssdoff +=size-inplace;
#ifndef CONCURRENT			
			userfs_write(abh);
			
			append_fsextent(ip,off+inplace,abh,1);
			bh_release(abh);
			insert_blktree(ip, ip->devid+1, addr, off+inplace, size-inplace);
#else
			wargument *warg1 = (wargument *)malloc(sizeof(wargument));
    		warg1->func_arg = abh;
    		//warg->mutex = mutex;
    		warg1->wflag = 1;
    		warg1->inode = ip;
    		warg1->off = off+inplace;
    		warg1->append =1;
    		ret = thpool_add_work(writepool[abh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",abh->b_dev,threadpool_queuesize(writepool[abh->b_dev]));
				panic("cannot add thread\n");
			}
    
#endif
			//append写直接插入到树中
			

		}else{  //完全inplace写
			userfs_info("inplace2 write off %d\n",off);
			
			delta =  (ip->rwoff-ip->rwsoff)>>g_block_size_shift;
			//userfs_info("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, (ip->rwoff-ip->rwsoff)>>g_block_size_shift, ip->rwsoff);
			if((delta+ (size>>g_block_size_shift)>512)||((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||(delta>=512)))){
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					
					case 1: 
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					case 2: 
						ip->rwoff = allocate_ssd2block();
						ip->devid=2;
						break;
					case 3:
						ip->rwoff = allocate_ssd3block();
						ip->devid=3;
						break;

					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->rwoff = ip->nvmoff;
						ip->rwnvmsize = ip->nvmoff;
						//ip->rwnvmsize += size>>g_block_size_shift;
						ip->devid=0;
						break;
					/*
					
					default:
						ip->rwoff = allocate_ssd1block();
						ip->devid=1;
						break;
					
					/*
					default:
						ip->nvmoff = allocate_nvmblock(512);
						ip->rwoff = ip->nvmoff;
						ip->rwnvmsize = ip->nvmoff;
						//ip->rwnvmsize += size>>g_block_size_shift;
						ip->devid=0;
						break;
					*/	
				}
				ip->rwsoff = ip->rwoff;
				//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->rwoff %ld, ip->wrthdnum %d, ip->rwsoff %lu\n",ip->inum,ip->devid, ip->rwoff,ip->wrthdnum,ip->rwsoff);
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}
			ibh = bh_get_sync_IO(ip->devid+1,ip->rwoff>>12,BH_NO_DATA_ALLOC);
			ibh->b_data=buf;
			ibh->b_size=size;
			ibh->b_offset=0;
			addr= ip->rwoff;
			ip->rwoff +=size;
#ifndef CONCURRENT			
			userfs_write(ibh);
			append_fsextent(ip,off,ibh,0);
			//upsert_blktree(ip, 1, addr, off, size);
			bh_release(ibh);
#else
			userfs_info("thpool_add_work size %d\n",ibh->b_size);
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = ibh;
   
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =2;
    		int ret = thpool_add_work(writepool[ibh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",ibh->b_dev,threadpool_queuesize(writepool[ibh->b_dev]));
				panic("cannot add thread\n");
			}

#endif
			//upsert_blktree(ip, 1, addr, off, size);
		}
	}
	return size;
}

int append_unaligned_smallwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){   //小于4K非对齐写
	//userfs_printf("unaligned_smallwrite off %d, write size %d, filesize %d\n",off,size,ip->size);
	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	struct buffer_head *bh;
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode = block_search(&(ip->blktree),off_aligned_4K);
	pthread_rwlock_unlock(&ip->blktree_rwlock);

	if(!blocknode){ //NULL
		bmap_req_t bmap_req;
		bmap_req.start_offset=off_aligned_4K;
		int ret=bmap(ip,&bmap_req);
		
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
	}
	if(blocknode){
		//userfs_info("blocknode fileoff %d, addr %ld\n", blocknode->fileoff,blocknode->addr);
		if(off==ip->size){   //append写,直接在后面写，追加size到blocknode->size
			bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((blocknode->fileoff-off_aligned_4K)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;
			bh->b_offset=off-off_aligned_4K;
#ifndef CONCURRENT	
			userfs_write(bh);
			append_fsextent(ip,off,bh,1);
			bh_release(bh);
			
#else
			wargument *warg = (wargument *)malloc(sizeof(wargument));
    		warg->func_arg = bh;
    		warg->wflag = 1;
    		warg->inode = ip;
    		warg->off = off;
    		warg->append =2;
    		int ret = thpool_add_work(writepool[bh->b_dev], userfs_write, (void *)warg);
			if(ret < 0){
				printf("writepool[%d] queue_size %d\n",bh->b_dev,threadpool_queuesize(writepool[bh->b_dev]));
				panic("cannot add thread\n");
			}
			
#endif
		
			blocknode->size += size;
			
		}
		else{
			userfs_printf("write not supported offset %d, ip->size %d\n",off,ip->size);
			panic("write not supported\n");
		}
	}else{
		panic("unaligned_smallwrite bug!!");
	}
	return size;

}

#define size_threshold 1024
int appendwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	userfs_info("aligned write filesize %d, off %d, size %d\n",ip->size,off,size);
	
	uint64_t writesize=0;
	struct buffer_head *abh;
	unsigned long addr;
	unsigned long size_4K = (size+4095)>>g_block_size_shift;
	uint8_t devid;
	if(off == ip->size){ //append写，直接写入后面   如果前半部分写到nvm，如何安置后半部分
		
		if(size < size_threshold){
			addr = allocate_nvmblock(size_4K);
			devid = 0;
			userfs_info("allocate_nvmblocks block %d, size %d, devid %d\n",size_4K,size, devid);
		}else{
			
			//userfs_printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld\n", ip->ladevid,ip->ssdoff,ip->ssdoff&0x1fffffUL);
			ip->wrthdnum = ip->wrthdnum % 3;
			addr = allocate_ssdblocks(size_4K,ip->wrthdnum);
			devid = ip->wrthdnum+1;
			userfs_info("allocate_ssdblocks block %d, size %d, devid %d\n",size_4K,size, devid);
			//userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->ladevid, ip->lassdoff,ip->wrthdnum);
			//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->ladevid, ip->lassdoff,ip->wrthdnum);
			__sync_fetch_and_add(&ip->wrthdnum,1);
			//ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址

		}
		
		/*
		ip->wrthdnum = ip->wrthdnum % writethdnum;
			
		switch(ip->wrthdnum){
			case 1: 
				addr = allocate_ssdblocks(size_4K,0);
				devid=1;
				break;
			case 2: 
				addr = allocate_ssdblocks(size_4K,1);
				devid=2;
				break;
			case 3:
				addr = allocate_ssdblocks(size_4K,2);
				devid=3;
				break;
			default:
				addr = allocate_nvmblock(size_4K);
				devid=0;
				//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
				break;
					
		}
		userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);		
		__sync_fetch_and_add(&ip->wrthdnum,1);
		*/
	 	abh = bh_get_sync_IO(devid+1,addr>>12,BH_NO_DATA_ALLOC);
		abh->b_data=buf;
		abh->b_size=size;
		abh->b_offset=0;

#ifndef CONCURRENT
		//printf("direct_write\n");
		userfs_write(abh);
		
		append_fsextent(ip,off,abh,1);
		bh_release(abh);
		insert_blktree(ip,ip->devid+1, addr, off, size);
#else
		//printf("threadpool_write\n");
    	wargument *warg = (wargument *)malloc(sizeof(wargument));
    	warg->func_arg = abh;
    	warg->wflag = 1;
    	warg->inode = ip;
    	warg->off = off;
    	warg->append =1;
		int ret = thpool_add_work(writepool[abh->b_dev], userfs_write, (void *)warg);
		if(ret < 0){
			printf("writepool[%d] queue_size %d\n",abh->b_dev,threadpool_queuesize(writepool[abh->b_dev]));
			panic("cannot add thread\n");
		}
#endif
	
	}
	return size;
}

int get_min(float a,float b, float c, float d){
	int ret=0;
	if(a>b){
		ret=1;
		a=b;
	}
	if(a>c){
		ret=2;
		a=c;
	}
	if(a>d){
		ret=3;
	}
	return ret;
}

float ssd1perratio = 0.125;//(1/8)
float ssd2perratio = 0.125;//(1/8)
float ssd3perratio = 0.125;//(1/8)
float nvmperratio = 0.625;//(5/8)

int ssd1writesize,ssd2writesize,ssd3writesize,nvmwritesize;
int write_scheduler(struct inode *inode, uint8_t *buf, offset_t off, uint32_t size){
	int retsize;
	
	float nvmwriteratio = nvmwritesize/(nvmperratio);
	float ssd1writeratio = ssd1writesize/(ssd1perratio);
	float ssd2writeratio = ssd2writesize/(ssd2perratio);
	float ssd3writeratio = ssd3writesize/(ssd3perratio);
	
	int devid= get_min(nvmwriteratio, ssd1writeratio,ssd2writeratio,ssd3writeratio);
	switch (devid){
	case 0:
		retsize=fs_nvmwrite(inode, buf, off, size);
		nvmwritesize+= retsize;	
		break;
	case 1:
		ssd1writesize+= size;
	case 2:
		ssd2writesize+= size;
		
	case 3:
		ssd3writesize+= size;
		//fuckfs_pwrites(devid,warg);
		break;
	}
}


int fs_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	int retsize;
	if((off+size) < ip->nvmoff){
		retsize=fs_nvmwrite(ip, buf, off, size);
	}else{

		retsize=fs_ssdwrite(ip, buf, off, size);
	}
	return retsize;
}


//如何防止一个块前面部分被写到nvm，后面被写到SSD中，
//根据offset对其到4k_off,如果小于filesize, 获得该(4K或2M)块地址，先在block tree中查找，再在in-nvm inode中查找

//先原地更新后append怎么写
int snfs_write(int fd, struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_printf("snfs_write filesize %d, off %d, size %d\n",ip->size,off, size);
	int retsize;


	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	
	uint32_t sized =size + off - off_aligned_4K;
	if(off_aligned_4K< off && sized >g_block_size_bytes){  //不对齐4K的情况
		//不对齐，且写的数据跨越一个块
		panic("write exist bug\n");
	}else if(off_aligned_4K< off){
		retsize = unaligned_smallwrite(ip,buf, off,size);
		
		return retsize;
	}

	if(off_aligned_4K == off){
		if(size < g_block_size_bytes){
			retsize=aligned_smallwrite(ip,buf, off,size);
			return retsize;
		}

		

		retsize= aligned_write(ip,buf, off,size);
		//retsize= appendwrite(ip,buf, off,size);
		return retsize;
	}

}

// Write to file f.
int userfs_file_write(struct file *f, uint8_t *buf, size_t n)
{
	//userfs_printf("userfs_file_write offset %d, size %d, ftype %d\n",f->off,n,f->type);

	int r;
	//uint32_t max_io_size = (128 << 20);
	uint32_t max_io_size = (2 << 20);
	offset_t i = 0, file_size;
	uint32_t io_size = 0;
	offset_t size_aligned, size_prepended, size_appended;
	offset_t offset_aligned, offset_start, offset_end;
	char *data;

	if (f->writable == 0)
		return -EPERM;
	
	struct  inode * inode =f->ip;
	file_size = inode->size;

	
	if (n > 0 && (f->off + n) > inode->size) 
		file_size = f->off + n;

	if (f->type == FD_INODE) {
		/*            a     b     c
		 *      (d) ----------------- (e)   (io data)
		 *       |      |      |      |     (4K alignment)
		 *           offset
		 *           aligned
		 *
		 *	a: size_prepended
		 *	b: size_aligned
		 *	c: size_appended
		 *	d: offset_start
		 *	e: offset_end
		 */

		userfs_debug("%s\n", "+++ start transaction");

		offset_start = f->off;
		offset_end = f->off + n;

		offset_aligned = ALIGN(offset_start, g_block_size_bytes);
		
		if (offset_end < offset_aligned) { 
			size_prepended = n;
			size_aligned = 0;
			size_appended = 0;
		} else {
			
			if (offset_start < offset_aligned) {
				size_prepended = offset_aligned - offset_start;
			} else
				size_prepended = 0;

			userfs_assert(size_prepended < g_block_size_bytes);

			
			// compute portion of appended unaligned write
			size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
			if (size_appended > 0)
				size_appended = g_block_size_bytes - size_appended;

			size_aligned = n - size_prepended - size_appended; 
			
		}
		userfs_printf("inode->inum %d, filesize %d, size_prepended %d, size_appended %d, size_aligned %d\n",inode->inum,inode->size,size_prepended,size_appended,size_aligned);
		
		if (size_prepended > 0) {
			
			ilock(inode);

			r = snfs_write(f->fd,inode,buf,f->off,size_prepended);
				//f->off += r;

			if (r > 0 && (f->off + r) > inode->size) 
				inode->size = f->off + r;
			iunlock(inode);

			userfs_assert(r > 0);

			f->off += r;

			i += r;
			
	
		}

		// add aligned portion to log
		
		uint64_t fileoff=0, aligned_2M_upoff;
		while(i < n - size_appended) {
			//userfs_info("while %d, %d\n",i, n - size_appended);
			userfs_assert((f->off % g_block_size_bytes) == 0);
			
			io_size = n - size_appended - i;

			if(io_size > max_io_size)
				io_size = max_io_size;

			fileoff = f->off + io_size;
			aligned_2M_upoff = (f->off + ((uint64_t)0x1<<g_lblock_size_shift)) & ~ (((uint64_t)0x1<<g_lblock_size_shift)-1);
			if(fileoff>= aligned_2M_upoff){
				io_size = aligned_2M_upoff - f->off;
			}
			
			ilock(inode);

			if ((r = snfs_write(f->fd,inode,buf+i,f->off,io_size)) > 0){
				if (r > 0 && (f->off + r) > inode->size) 
					inode->size = f->off + r;
				f->off += r;
			}

			iunlock(inode);

			if(r < 0)
				break;
			//userfs_info("return r is %d, io_size is %d\n",r,io_size);
			if(r != io_size)
				panic("short filewrite");

			i += r;
		
		}

		if (size_appended > 0) {

			ilock(inode);
			r = snfs_write(f->fd,inode,buf+i,f->off,size_appended);
			
			if (r > 0 && (f->off + r) > inode->size) 
				inode->size = f->off + r;

			iunlock(inode);

			userfs_assert(r > 0);

			f->off += r;

			i += r;


		}

	
		userfs_debug("%s\n", "--- end transaction");
		
		inode->size = file_size;
		return i == n ? n : -1;
	}

	panic("filewrite");

	return -1;
}

struct inode *fetch_ondisk_inode_name(char *path)
{
	
	struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
	struct inode * parent_inode = NULL;
	int ret;
	ret = syscall(328, path, tinode, 0, 1, 0);
	
	if(ret>0){
		
		parent_inode = inodecopy(tinode);
		art_tree_init(&parent_inode->dentry_tree);
		parent_inode->inmem_den =0;
	}
	return parent_inode;
}


struct inode *read_ondisk_inode_from_kernel(uint32_t inum){
	userfs_info("read_ondisk_inode_from_kernel fetch inode ino %d\n",inum);
	int ret=0;
	struct inode *inode;
	inode=(struct  inode *)malloc(sizeof(struct inode));
	struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
	//ret=syscall(328, NULL, tinode, inum, 0, 0 );
	ret=syscall(336,inum, tinode);
	if(ret<0){
		panic("syscall fetch inode error\n");
	}
	inode->i_mode = tinode->i_mode;
  
    inode->flags |= I_VALID;
    inode->inum = inum;
    inode->nlink = tinode->i_nlink;
    inode->size = tinode->i_size;
    inode->ksize = tinode->i_size;
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
    //inode->i_state = tinode->i_state;
	inode->height = tinode->height;
    inode->root = tinode->root;
    inode->i_blk_type = tinode->i_blk_type;
    inode->nvmblock = tinode->nvmblock;
    uint8_t devid = tinode->blkoff>>62;
    if(devid==0){
    	inode->nvmoff = tinode->blkoff;
    	inode->ssdoff =0;
    }else{
    	inode->ssdoff = tinode->blkoff &(((uint64_t)0x1<<62)-1);
    	inode->devid =devid;
    	inode->nvmoff =0;
    }
    inode->wrthdnum=0;
    //userfs_info("inode->root %lu\n",inode->root);
    return inode;
}


//char mountpoint[DIRSIZ] = "/mnt/pmem_emul";
int isroot(char *pathname){
	return strcmp(mountpoint,pathname);
}


struct inode * userfs_open_file(char *path, unsigned short type)
{
	userfs_printf("userfs_open_file %s\n",path);
	offset_t offset;
	struct inode *parent_inode = NULL;
	struct inode *inode = NULL;
	char name[DIRSIZ], parent_path[MAX_PATH], filename[DIRSIZ];
	uint64_t tsc_begin, tsc_end;
	int ret;
	uint32_t inum;
	pthread_rwlockattr_t rwlattr;


	inode=dlookup_find(path);
	if(inode!=NULL)
	{
		userfs_info("dir_lookup inode path %s\n",path);
		inode->i_ref++;
		return inode;
	}

	
	get_parent_path(path,parent_path,filename);
	userfs_info("path is %s, parent_path is %s, and filename is %s\n",path,parent_path,filename);
	if ((parent_inode = nameiparent(path, name)) == 0){  //在hash table中查找inode
		userfs_printf("parent directory %s does not exist!!!!\n",parent_path);
		return NULL;
		
		parent_inode=fetch_ondisk_inode_name(parent_path);//bug
		
		dlookup_alloc_add(parent_inode,parent_path);    //HASH_ADD_STR 插入inode
	}
	
	inode=de_cache_find(parent_inode,name);
	if(inode!=NULL){
		inode->i_ref++;
		return inode;
	}

	ilock(parent_inode);
	if (enable_perf_stats) 
		tsc_begin = asm_rdtscp();


#if 0
	if(!strcmp(testdir,parent_path)){
		testinode = parent_inode;
	}
	if(testinode!=NULL){
		printf("dentry_size %d\n", art_size(&testinode->dentry_tree));
		printf("testname is %s, namelen is %d\n", testname,strlen(testname));
		struct ufs_direntry *testuden=art_search(&testinode->dentry_tree, testname, strlen(testname));
		if(testuden==NULL){
			printf("NULL testuden\n");
		}
	}
#endif
	userfs_info("filename is %s, parent_inode->dentry_tree %lu\n", filename,&parent_inode->dentry_tree);
	pthread_rwlock_rdlock(&parent_inode->art_rwlock);
	struct ufs_direntry *uden=art_search(&parent_inode->dentry_tree, filename, strlen(filename));
	pthread_rwlock_unlock(&parent_inode->art_rwlock);

	if(uden == NULL && parent_inode->root>0 && parent_inode->inmem_den==0){
		uden=fetch_ondisk_dentry_from_kernel(parent_inode,name);
		fetch_ondisk_dentry(parent_inode);
		parent_inode->inmem_den=1;
		
	}
	if(uden == NULL || uden->addordel==Delden){ //不存在此文件dentry，即不存在此文件或文件被删除
		userfs_printf("not exist file %s or dir \n",path);
		if(uden==NULL){
			uden= (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
		}
		
		struct fs_direntry * direntry= (struct fs_direntry *)malloc(sizeof(struct fs_direntry));

		inum =find_inode_bitmap();
		if(inum==0){
			panic("cannot allocate inode\n");
			return NULL;
		}
		
		struct timeval curtime;
		
		inode=icache_alloc_add(inum);
		inode->inum = inum;
		inode->size =0;
		inode->ksize=0;
		inode->flags = 0;
		inode->flags |= I_VALID;
		inode->i_ref = 1;
		inode->n_de_cache_entry = 0;
		inode->i_dirty_dblock = RB_ROOT;
		inode->i_sb = sb;
		inode->fcache = NULL;
		inode->n_fcache_entries = 0;
		inode->devid=0;
		inode->wrthdnum =0;
		inode->nvmblock=0;
		inode->ssdoff=0;
		inode->nvmoff=0;
		inode->rwoff = 0;
		inode->rwsoff=0;
		inode->root=0;
		inode->fd=-1;
	
		pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

		pthread_rwlock_init(&inode->fcache_rwlock, &rwlattr);

#ifdef KLIB_HASH
		inode->fcache_hash = kh_init(fcache);
#endif
		//inode->fcache_hash = kh_init(fcache);

		if (!inode)
			panic("cannot create inode");
		
		if (enable_perf_stats) {
			tsc_end = asm_rdtscp();
			g_perf_stats.ialloc_tsc += (tsc_end - tsc_begin);
			g_perf_stats.ialloc_nr++;
		}

		inode->itype = type;
		inode->nlink = 1;
		
		inode->ssdsize=0;

		inode->blktree=RB_ROOT;
		pthread_rwlock_init(&inode->blktree_rwlock, &rwlattr);

		inode->de_cache = NULL;
		pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&inode->i_mutex, NULL);

		
		userfs_printf("create %s - inum %u\n", path, inode->inum);
		userfs_debug("create %s - inum %u\n", path, inode->inum);

		if (type == T_DIR) {
			
			parent_inode->nlink++;
			inode->size = FUCKFS_DIR_REC_LEN(1)+FUCKFS_DIR_REC_LEN(2);
	#if 0
			if (dir_add_entry(inode, ".", inode->inum) < 0)
				panic("cannot add . entry");

			if (dir_add_entry(inode, "..", parent_inode->inum) < 0)
				panic("cannot add .. entry");
	#endif


			if (ret)
				userfs_debug("%s ERROR %d: %s\n", __func__, ret, path);
			
			pthread_rwlock_init(&inode->art_rwlock, &rwlattr);
			art_tree_init(&inode->dentry_tree);
			inode->inmem_den = 0;
			
		}
		
		direntry->ino = inum;
		direntry->file_type=type;
		strncpy(direntry->name,filename,DIRSIZ);
		
		uden->addordel=Addden;
		uden->direntry=direntry;

		
		pthread_rwlock_wrlock(&parent_inode->art_rwlock);
		ret = art_insert(&parent_inode->dentry_tree, filename, strlen(filename), uden);
		pthread_rwlock_unlock(&parent_inode->art_rwlock);
		
		int namelen = strlen(filename);
		parent_inode->size +=FUCKFS_DIR_REC_LEN(namelen);
		struct setattr_logentry *attrentry = (struct setattr_logentry *)malloc(sizeof(struct setattr_logentry));
		attrentry->ino = inum;
		attrentry->entry_type=SET_ATTR;
		attrentry->attr=0;
		attrentry->mode=0;
		attrentry->uid=0;
		attrentry->gid=0;
		attrentry->isdel=0;
		//attrentry->isnew=1;
		gettimeofday(&curtime,NULL);
		attrentry->mtime=curtime.tv_sec;
		attrentry->ctime=curtime.tv_sec;
		attrentry->size=0;
		append_entry_log(attrentry,0);
		
		struct fuckfs_dentry *fdentry = (struct fuckfs_dentry *)malloc(sizeof(struct fuckfs_dentry));
		fdentry->entry_type=DIR_LOG;
		fdentry->ino=parent_inode->inum;
		//fdentry->entry_type=type;
		fdentry->name_len= namelen;               /* length of the dentry name */
		fdentry->file_type=type;              /* file type */
		fdentry->addordel=Addden;
		fdentry->invalid=1;		/* Invalid now? */
		//fdentry->de_len= ;                 /* length of this dentry */
		fdentry->links_count=1;
		fdentry->mtime =curtime.tv_sec;			/* For both mtime and ctime */
		fdentry->fino =inum;                    /* inode no pointed to by this entry */
		strncpy(fdentry->name,filename,DIRSIZ);	/* File name */
		append_entry_log(fdentry, 1);

		
	}else{
		
		inode=read_ondisk_inode_from_kernel(uden->direntry->ino);
		inode = icache_add(inode);
		inode->itype =type;

		inode->i_ref = 1;
		inode->n_de_cache_entry = 0;
		inode->i_dirty_dblock = RB_ROOT;
		inode->i_sb = sb;
		pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);
		pthread_rwlock_init(&inode->fcache_rwlock, &rwlattr);
		inode->fcache = NULL;
		inode->n_fcache_entries = 0;
		inode->fd=-1;
#ifdef KLIB_HASH
		inode->fcache_hash = kh_init(fcache);
#endif
		//inode->fcache_hash = kh_init(fcache);

		inode->nlink = 1;

		inode->blktree=RB_ROOT;
		pthread_rwlock_init(&inode->blktree_rwlock, &rwlattr);
		inode->de_cache = NULL;
		pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&inode->i_mutex, NULL);

		if (type == T_DIR) {
			parent_inode->nlink++;
			pthread_rwlock_init(&inode->art_rwlock, &rwlattr);
			art_tree_init(&inode->dentry_tree);
			inode->inmem_den = 0;
			
		}

	}

	iunlockput(parent_inode);

	


	de_cache_alloc_add(parent_inode, name, inode);

	
	if (!dlookup_find(path))
		dlookup_alloc_add(inode, path);  //HASH_ADD_STR(inode)
	//dlookup_find(path);
	//userfs_info("end userfs_open_file %lx\n",inode);
	userfs_printf("userfs_open_file sucesss path %s, ref %d\n",path,inode->i_ref);
	return inode;
}