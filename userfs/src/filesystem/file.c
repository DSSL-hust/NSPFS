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
//#include "log/log.h"
#include "concurrency/synchronization.h"
#include <math.h>
#include <pthread.h>
#include "ds/blktree.h"

struct open_file_table g_fd_table;

int curnvmnum=0;
int curssdnum=0;
#define THREAD_NUM 1024
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#if 0
char testdir[256]="/mnt/pmem_emul/bigfileset/00000001/00000005/00000001";
char testname[256]="00000005";
struct inode * testinode;
#endif


void userfs_file_init(void)
{
	//printf("userfs_file_init\n");
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_spin_init(&g_fd_table.lock, PTHREAD_PROCESS_SHARED);
	//pthread_rwlock_init(&g_fd_table.ftlock, &rwlattr);
	
	struct file *f= &g_fd_table.open_files[0];
	for(int i = 0; i < g_max_open_files; i++, f++) {
		memset(f, 0, sizeof(*f));
		pthread_rwlock_init(&f->rwlock, &rwlattr);
	}
	
	printf("userfs_file_init end\n");

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
	//userfs_info("userfs_file_alloc g_fd_table.lock %d\n",g_fd_table.lock);
	pthread_spin_lock(&g_fd_table.lock);
	//int ret = pthread_rwlock_wrlock(&g_fd_table.ftlock);
	for(i = 0, f = &g_fd_table.open_files[0]; 
			i < g_max_open_files; i++, f++) {
		
		//
		if(f->ref == 0) {
			pthread_rwlock_wrlock(&f->rwlock);
			userfs_info("userfs_file_alloc return fd %d, f->ref %d\n",i,f->ref);
			//int ret = pthread_rwlock_destroy(&f->rwlock);

			//if(ret<0){
			//	panic("error\n");
			//}
			//userfs_info("fd %d pthread_rwlock_destroy \n",i);
			//memset(f, 0, sizeof(*f));
			f->ref = 1;
			f->fd = i;
			pthread_rwlock_unlock(&f->rwlock);
			//pthread_rwlock_init(&f->rwlock, &rwlattr);
			//printf("f->rwlock %d\n",&f->rwlock);
			//pthread_rwlock_unlock(&f->rwlock);
			pthread_spin_unlock(&g_fd_table.lock);
			//pthread_rwlock_unlock(&g_fd_table.ftlock);
			//userfs_info("userfs_file_alloc f->ref %d\n",f->ref);
			return f;
		}else{
			//userfs_info("userfs_file_alloc fd %d, f->ref %d, fname %s\n",i,f->ref,f->filename);
		}
		//pthread_rwlock_unlock(&f->rwlock);
	}

	
	pthread_spin_unlock(&g_fd_table.lock);
	//pthread_rwlock_unlock(&g_fd_table.ftlock);
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
/*
void reclaim_block(struct inode* inode){
	userfs_info("reclaim_block allonvmsize %d, allossdsize %d\n",inode->allonvmsize, inode->allossdsize);
	//struct space *nvmspace = inode->nvmspace;
	//struct space *ssdspace =inode->ssdspace;
	struct space *allonvmspace = inode->allonvmspace;
	struct space *allossdspace = inode->allossdspace;

	struct space *tmpspace;
	struct list_head *pos,*node;
	int i;
	userfs_info("reclaim_block allonvmsize %d\n",inode->allonvmsize);
	if(inode->allonvmsize>0){
		list_for_each_safe(pos,node,&(allonvmspace->list))
		{
			tmpspace=list_entry(pos, struct space, list);
			if(tmpspace){
				userfs_info("tmpspace->size is %d, startaddr %d, endaddr %d\n", tmpspace->size,tmpspace->startaddr,tmpspace->endaddr);
			//for(i=0;i<srangenum;i++){
				pthread_mutex_lock(nblock_lock);
				for(i=0;i<nrangenum;i++){
					printf("reclaim_nvm_block\n");
					userfs_info("nblock_table[%d] %d --- %d\n",i,nblock_table[i][0],nblock_table[i][1]);
					if(nblock_table[i][1]<=0||nblock_table[i][0]==nblock_table[i][1]){
						nblock_table[i][0]=tmpspace->startaddr;
						nblock_table[i][1]=tmpspace->endaddr;
						break;

					}else{
						if(tmpspace->endaddr+1==nblock_table[i][0]){
							nblock_table[i][0]=tmpspace->startaddr;
							break;
						}
					}	
				}
				pthread_mutex_unlock(nblock_lock);
				userfs_info("nblock_table[%d] %d --- %d\n",i,nblock_table[i][0],nblock_table[i][1]);
				if(curnvmnum<i)
					curnvmnum=i;
			}else{
				break;
			}
			list_del(pos);
		}
		inode->allonvmsize=0;

	}
	userfs_info("reclaim_block allossdsize %d\n",inode->allossdsize);
	if(inode->allossdsize>0){
		list_for_each_safe(pos,node,&(allossdspace->list))
		{
			tmpspace=list_entry(pos, struct space, list);
			if(tmpspace){
				userfs_info("tmpspace->size is %d, startaddr %d, endaddr %d\n", tmpspace->size,tmpspace->startaddr,tmpspace->endaddr);
			//for(i=0;i<srangenum;i++){
				pthread_mutex_lock(sblock_lock);
				for(i=0;i<srangenum;i++){
					//printf("reclaim_ssd_block\n");
					userfs_info("sblock_table[%d] %d --- %d\n",i,sblock_table[i][0],sblock_table[i][1]);
					if(sblock_table[i][1]<=0||sblock_table[i][0]==sblock_table[i][1]){
						sblock_table[i][0]=tmpspace->startaddr;
						sblock_table[i][1]=tmpspace->endaddr;

						break;
					}else{
						if(tmpspace->endaddr+1==sblock_table[i][0]){
							sblock_table[i][0]=tmpspace->startaddr;
							break;
						}
					}
				}
				pthread_mutex_unlock(sblock_lock);
				userfs_info("sblock_table[%d] %d --- %d\n",i,sblock_table[i][0],sblock_table[i][1]);
				if(curssdnum<i)
					curssdnum=i;
			}else{
				break;
			}
			list_del(pos);		
		}
		inode->allossdsize=0;
		
	}

	printf("reclaim_block usednvmsize %d, usedssdsize %d\n",inode->usednvmsize, inode->usedssdsize);
	if(inode->usednvmsize>0){
		list_for_each(pos,&(usednvmspace->list))
		{
			tmpspace=list_entry(pos, struct space, list);
			if(tmpspace){
				printf("tmpspace->size is %d, startaddr %d, endaddr %d\n", tmpspace->size,tmpspace->startaddr,tmpspace->endaddr);
			//for(i=0;i<srangenum;i++){
				for(i=0;i<nrangenum;i++){
					printf("reclaim_nvm_block\n");
					printf("nblock_table[%d] %d --- %d\n",i,nblock_table[i][0],nblock_table[i][1]);
					if(nblock_table[i][1]<=0){
						nblock_table[i][0]=tmpspace->startaddr;
						nblock_table[i][1]=tmpspace->endaddr;
						break;

					}else{
						if(tmpspace->endaddr+1==nblock_table[i][0]){
							nblock_table[i][0]=tmpspace->startaddr;
							break;
						}
					}	
				}
				printf("nblock_table[%d] %d --- %d\n",i,nblock_table[i][0],nblock_table[i][1]);
			}else{
				break;
			}
			list_del(pos);
			
		}
		inode->usednvmsize=0;
	}

	if(inode->usedssdsize>0){
		list_for_each(pos,&(usedssdspace->list))
		{
			tmpspace=list_entry(pos, struct space, list);
			if(tmpspace){
				printf("tmpspace->size is %d, startaddr %d, endaddr %d\n", tmpspace->size,tmpspace->startaddr,tmpspace->endaddr);
			//for(i=0;i<srangenum;i++){
				for(i=0;i<srangenum;i++){
					printf("reclaim_ssd_block\n");
					printf("sblock_table[%d] %d --- %d\n",i,sblock_table[i][0],sblock_table[i][1]);
					if(sblock_table[i][1]<=0){
						sblock_table[i][0]=tmpspace->startaddr;
						sblock_table[i][1]=tmpspace->endaddr;
						break;

					}else{
						if(tmpspace->endaddr+1==sblock_table[i][0]){
							sblock_table[i][0]=tmpspace->startaddr;
							break;
						}
					}	
				}
				printf("sblock_table[%d] %d --- %d\n",i,sblock_table[i][0],sblock_table[i][1]);
			}else{
				break;
			}
			list_del(pos);
			
		}
		inode->usedssdsize=0;
	}
	
	userfs_info("reclaim_block success %lu\n",inode);
}
*/
int userfs_file_close(struct file *f)
{
	//userfs_info("userfs_file_close filename %s\n",f->filename);

	struct file ff;

	
	userfs_assert(f->ref > 0);

	struct  inode * inode = f->ip;
	pthread_rwlock_wrlock(&f->rwlock);
	//userfs_info("fd %d pthread_rwlock_wrlock \n",f->fd);

	if(--f->ref > 0) {
		if(f->type==FD_INODE)
			iput(inode);
		//userfs_info("f->ref %d, inode->i_ref %d\n",f->ref, inode->i_ref);
		pthread_rwlock_unlock(&f->rwlock);
		//userfs_info("userfs_file_close success fd %lu, f->ref %d, inode->i_ref %d\n",f->fd+g_fd_start, f->ref,inode->i_ref);
		return 0;
	}
	
	
	//inode->i_ref=0;
	/*
	pthread_t ntid;
	int err = pthread_create(&ntid, NULL, devicecommit, NULL);
	if(err!=0){
		printf("create thread failed:%s\n",strerror(err));
		exit(-1);
	}else{
		printf("device_commit\n");
	}
	*/

	// need clear rbtree?
	//block_clear(&(inode->blktree));
	//fcache_del_all(inode);



	ff = *f;
	//return nvm and ssd space
	//reclaim_block(f->ip);
	f->type = FD_NONE;
	f->ref = 0;
	//userfs_info("f->ref %d\n",f->ref);
	//inode->i_ref=0;
	

	pthread_rwlock_unlock(&f->rwlock);
	//userfs_info("fd %d pthread_rwlock_unlock \n",f->fd);
	//userfs_info("f->ref %d\n",f->ref);


	if(ff.type == FD_INODE) 
		iput(ff.ip);

	//devicecommit();
	//threadpool_destroy(committhdpool,1);
	//userfs_info("userfs_file_close success fd %lu, f->ref %d, inode->i_ref %d\n",f->fd+g_fd_start, f->ref,inode->i_ref);
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
	//int r = 0;
	struct timespec ts_end,ts_end1,ts_end2;
	clock_gettime(CLOCK_REALTIME, &ts_end);
	

	//printf("userfs_file_read0 %d\n",f->readable);
	if (f->readable == 0) 
		return -EPERM;
	//userfs_printf("userfs_file_read f->type %d\n",f->type);
	if (f->type == FD_INODE) {
		ilock(f->ip);
		userfs_info("userfs_file_read f->off %d, f->ip->size %d, read size %d, inum %d\n",f->off,f->ip->size,n,f->ip->inum);
		if (f->off >= f->ip->size) {
			iunlock(f->ip);
			return 0;
		}
		//printf("readi %d\n",n);
		clock_gettime(CLOCK_REALTIME, &ts_end1);
		
		int r = readi(f->ip, buf, f->off, n);
		clock_gettime(CLOCK_REALTIME, &ts_end2);
		userfs_printf("0 readendtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end.tv_sec, ts_end.tv_nsec);
		userfs_printf("1 readi endtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end1.tv_sec, ts_end1.tv_nsec);
		userfs_printf("2 readi endtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end2.tv_sec, ts_end2.tv_nsec);
		//userfs_info("success read retsize %d\n",r);
		//printf("buf %s\n", buf);
		//printf("read r %d\n", r);
		if (r < 0) 
			panic("read error\n");

		//ilock(f->ip);
		f->off += r;

		iunlock(f->ip);
		//clock_gettime(CLOCK_REALTIME, &ts_end2);
		//userfs_printf("2 readi endtime : tv_sec=%ld, tv_nsec=%ld\n", ts_end2.tv_sec, ts_end2.tv_nsec);
		return r;
	}
	//lru_upsert(f->ip->inum,f->filename);
	
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
	//panic("userfs_file_read_offset\n");

	return -1;
}

/*
static void allocate_block(struct inode* inode, uint32_t allocate_NVMBlock, uint32_t allocate_SSDBlock){
	userfs_info("allocate_block allocate_NVMBlock %d, allocate_SSDBlock %d\n",allocate_NVMBlock,allocate_SSDBlock);
	uint32_t size, allnvm,allssd;
	int i;
	allocate_NVMBlock=allocate_NVMBlock-inode->usednvms;
	allnvm = allocate_NVMBlock; 
	allssd = allocate_SSDBlock;
	struct space *tmpspace,*allospace;
	//struct space nvmspace,ssdspace;
	//INIT_LIST_HEAD(&nvmspace.list);
	//INIT_LIST_HEAD(&ssdspace.list);

	//for(i=0;i<nrangenum;i++){
	if(!(inode->allocatednvm)){  //是否分配nvm
		//printf("total allocate_block %d\n",allocate_NVMBlock);
		//for(i=0;i<nrangenum;i++){
		userfs_info("allocate nvm curnvmnum %d\n", curnvmnum);
		pthread_mutex_lock(nblock_lock);
		for(i=curnvmnum;i>=0;i--){
		//i=curnvmnum;
		//while(i>=0){
			userfs_info("allocate_block1 %d\n",i);
			size = nblock_table[i][1]-nblock_table[i][0];
			struct space *tmpspace,*allospace;
			tmpspace =(struct space*)malloc(sizeof(struct space));
			allospace =(struct space*)malloc(sizeof(struct space));

			userfs_info("allocate nvmblock size %d\n",size);
			userfs_info("nblock_table[%d] %d --- %d\n",i,nblock_table[i][0],nblock_table[i][1]);
			if(size>0){
				if(size>=allnvm){
					tmpspace->startaddr = nblock_table[i][0];
					allospace->startaddr = nblock_table[i][0];
					tmpspace->endaddr = nblock_table[i][0]+allnvm-1;
					allospace->endaddr = nblock_table[i][0]+allnvm-1;

					nblock_table[i][0] = nblock_table[i][0]+allnvm;
					tmpspace->size = allnvm;
					allospace->size = allnvm;
					allnvm = 0;
				}else{  //size < allnvm
					allospace->startaddr = tmpspace->startaddr = nblock_table[i][0];
					allospace->endaddr = tmpspace->endaddr = nblock_table[i][1];
					allospace->size = tmpspace->size =size+1;
					allnvm = allnvm - tmpspace->size;
					nblock_table[i][0] = 0;
					nblock_table[i][1] = 0;
					curnvmnum=i;
				}
				//printf("allocate_block2 %lu\n",&(inode->nvmspace->list));
				//printf("nblock_table[%d] %d --- %d\n",i,nblock_table[i][0],nblock_table[i][1]);
				//printf("tmpspace %lu\n", tmpspace);
				userfs_info("allospace nvm size %d --> %d------%d\n", allospace->size, allospace->startaddr,allospace->endaddr);
				list_add_tail(&(tmpspace->list),&(inode->nvmspace->list));
				list_add_tail(&(allospace->list),&(inode->allonvmspace->list));
				//inode->allonvmsize+=allospace->size;
				userfs_info("allocate_block allnvm %d\n",allnvm);
				if(allnvm<=0){
					break;
				}
				//printf("allocate_block4\n");
			}
			//i--;
			//printf("i %d\n",i);
		}
		pthread_mutex_unlock(nblock_lock);
		inode->allocatednvm=1;
		inode->nvmsize += allocate_NVMBlock - allnvm;
		userfs_info("allocate_block inode->nvmsize %d\n",inode->nvmsize);
	}
	//for(i=0;i<srangenum;i++){
	//for(i=0;i<srangenum;i++){
	userfs_info("allocate ssd curssdnum %d\n", curssdnum);
	pthread_mutex_lock(sblock_lock);
	for(i=curssdnum;i>=0;i--){

		size = sblock_table[i][1]-sblock_table[i][0];
		struct space *tmpspace,*allospace;
		tmpspace =(struct space*)malloc(sizeof(struct space));
		allospace =(struct space*)malloc(sizeof(struct space));
		userfs_info("allocate ssdblock size %d\n",size);
		userfs_info("sblock_table[%d] %d --- %d\n",i,sblock_table[i][0],sblock_table[i][1]);
		if(size>0){
			if(size>=allssd){
				allospace->startaddr = tmpspace->startaddr = sblock_table[i][0];
				allospace->endaddr = tmpspace->endaddr = sblock_table[i][0]+allssd-1;
				sblock_table[i][0] = sblock_table[i][0]+allssd;
				allospace->size = tmpspace->size = allssd;
				allssd = 0;
			}else{  //size < allssd
				allospace->startaddr = tmpspace->startaddr = sblock_table[i][0];
				allospace->endaddr = tmpspace->endaddr = sblock_table[i][1];
				allospace->size = tmpspace->size =size+1;
				allssd = allssd -tmpspace->size;
				sblock_table[i][0] = 0;
				sblock_table[i][1] = 0;
				curssdnum=i;
			}
			//printf("sblock_table[%d] %d --- %d\n",i,sblock_table[i][0],sblock_table[i][1]);
			list_add_tail(&(tmpspace->list),&(inode->ssdspace->list));
			list_add_tail(&(allospace->list),&(inode->allossdspace->list));
			userfs_info("allospace ssd tmpspace size %d --> %d------%d\n", tmpspace->size, tmpspace->startaddr,tmpspace->endaddr);
			userfs_info("allospace ssd size %d --> %d------%d\n", allospace->size, allospace->startaddr,allospace->endaddr);
			inode->allossdsize+=allospace->size;
			//inode->ssdsize +=tmpspace->size;
			//printf("allocate_block allssd %d\n",allssd);
			if(allssd<=0){
				break;
			}
		}
	}
	pthread_mutex_unlock(sblock_lock);
	inode->ssdsize += allocate_SSDBlock - allssd;

	userfs_info("allocate_block success inode->nvmsize %d, inode->ssdsize %d\n",inode->nvmsize,inode->ssdsize);
	//inode->nvmspace =&nvmspace;
	//inode->ssdspace = &ssdspace;

	struct list_head *pos, *node;
	struct space *getspace;
	list_for_each_safe(pos,node,&(inode->ssdspace->list))
	{
		getspace = list_entry(pos, struct space, list);
		printf("ssd getspace->startaddr %d, getspace->endaddr %d,getspace->size %d\n",getspace->startaddr, getspace->endaddr,getspace->size);
	}

}
*/

/*
int findspace(struct inode *inode, size_t blocknum ,struct space *space){ //如何返回地址
	userfs_info("findspace %d\n",blocknum);
	int flag=0;
	//int isnvm=-1;
	struct space *getspace,*tmpspace;
	struct list_head *pos,*node;
	
	//uint32_t blocknum = (uint32_t)ceil((float)n/(float)g_block_size_bytes);  //还需分配多少
	uint32_t allocnum = blocknum;  //最终分配了多少
fsp:
	userfs_info("inode->nvmsize %d\n", inode->nvmsize);
	userfs_info("inode->ssdsize %d\n", inode->ssdsize);
	
	//printf("blocknum %d\n", blocknum);
	if(inode->nvmsize < blocknum){
		if(inode->ssdsize < blocknum){
			uint32_t nvmsize = 256, ssdsize = 4096;
			
			//pthread_mutex_lock(block_lock);
			allocate_block(inode,nvmsize,ssdsize);
			//pthread_mutex_unlock(block_lock);
			if(flag == 0){
				flag++;
				goto fsp;
			}else{
				panic("allocate error\n");
			}
		}
		//分配ssd空间
		userfs_info("find ssd space %d\n",blocknum);
		struct space *ssdspace =inode->ssdspace;
		list_for_each_safe(pos,node,&(ssdspace->list))
		{

			tmpspace =(struct space*)malloc(sizeof(struct space));
			getspace = list_entry(pos, struct space, list);
			if(getspace==NULL){
				panic("error\n");
			}
			userfs_info("ssd getspace->startaddr %d, getspace->endaddr %d,getspace->size %d\n",getspace->startaddr, getspace->endaddr,getspace->size);
			if(getspace->size>blocknum){
				getspace->size = getspace->size -blocknum;
				tmpspace->startaddr= (getspace->startaddr);//<<g_block_size_shift;// 转换成字节地址
				tmpspace->endaddr = (getspace->startaddr+ blocknum-1);//<<g_block_size_shift;
				tmpspace->size = blocknum;// <<g_block_size_shift;
				getspace->startaddr +=blocknum;
				blocknum=0;
				//if(getspace->size==0){
				//	list_del(pos);
				//}
			}else{
				tmpspace->startaddr = (getspace->startaddr);//<<g_block_size_shift;
				tmpspace->endaddr = (getspace->endaddr);//<<g_block_size_shift;
				tmpspace->size = (getspace->size);//<<g_block_size_shift;
				blocknum=blocknum-getspace->size;

				list_del(pos);
				//getspace = list_entry(pos, struct space, list);
				//printf("sp ssd getspace->startaddr %d, getspace->endaddr %d,getspace->size %d\n",getspace->startaddr, getspace->endaddr,getspace->size);

			}
			userfs_info("ssd tmpspace->startaddr %d, tmpspace->endaddr %d,tmpspace->size %d\n",tmpspace->startaddr, tmpspace->endaddr,tmpspace->size);
			list_add_tail(&(tmpspace->list),&(space->list));
			//list_add_tail(&(tmpspace->list),&(inode->usedssdspace->list));
			//inode->usedssdsize+=tmpspace->size;
			if(blocknum==0){
				break;
			}
		}
		allocnum -= blocknum;
		inode->ssdsize =inode->ssdsize- allocnum;
		return 1;

	}else{
		//分配nvm空间
		userfs_info("find nvm space %d\n",blocknum);
		struct space *nvmspace = inode->nvmspace;
		list_for_each_safe(pos,node, &(nvmspace->list))
		{
			//printf("in list_for_each\n");
			tmpspace =(struct space*)malloc(sizeof(struct space));
			getspace = list_entry(pos, struct space, list);
			//printf("get space\n");
			//printf("getspace %lu\n", getspace);
			//printf("nvm getspace->startaddr %d, getspace->endaddr %d,getspace->size %d\n",getspace->startaddr, getspace->endaddr,getspace->size);
			if(getspace->size>blocknum){
				//printf("allocate_space size>blocknum\n");
				getspace->size = getspace->size -blocknum;
				tmpspace->startaddr= getspace->startaddr;//<<g_block_size_shift;
				tmpspace->endaddr = getspace->startaddr+ blocknum-1;//<<g_block_size_shift;
				tmpspace->size =blocknum;//<<g_block_size_shift;
				getspace->startaddr +=blocknum;
				blocknum=0;
				//if(getspace->size==0){
				//	list_del(pos);
				//}
				//break;
			}else{
				//printf("allocate_space size<=blocknum\n");
				tmpspace->startaddr = (getspace->startaddr);//<<g_block_size_shift;
				tmpspace->endaddr = (getspace->endaddr);//<<g_block_size_shift;
				tmpspace->size = (getspace->size);//<<g_block_size_shift;
				blocknum=blocknum-getspace->size;
				//printf("list_del(pos)\n");
				list_del(pos);
				

			}
			userfs_info("nvm tmpspace->startaddr %d, tmpspace->endaddr %d,tmpspace->size %d\n",tmpspace->startaddr, tmpspace->endaddr,tmpspace->size);
			list_add_tail(&(tmpspace->list),&(space->list));
			//list_add_tail(&(tmpspace->list),&(inode->allonvmspace->list));
			//inode->usednvmsize+=tmpspace->size;
			if(blocknum==0){
				break;
			}
		}
		allocnum -= blocknum;
		inode->nvmsize =inode->nvmsize- allocnum;
		return 0;
	}

	//printf("allocate return -1\n");
	return -1;
}
*/
/*
struct write_arg{
	struct inode *inode;
	uint8_t *buf;
	offset_t off;
	size_t size;
	offset_t addr;
};

uint32_t nvmstartaddr= 268435456;
uint32_t ssdstartaddr= 0;
int writenum=0;
int isnvm=0;

static size_t fuckfs_pwriten(void *arg){
	//printf("fs_pwriten\n");
	struct write_arg *warg;
	warg = (struct write_arg *)arg;
	struct  inode *inode = warg->inode;
	uint8_t *buf = warg->buf;
	offset_t off = warg->off;
	size_t size = warg->size;
	//offset_t addr = warg->addr;
	char *writeaddr=inode->writespace;
	//printf("memcpy %d\n",addr);
	//userfs_write_nvm(buf,addr,size);
	//printf("write addr %d\n", writeaddr+off);
	memcpy(writeaddr+off, buf,size);
	//msync(mmapstart+addr, size,PROT_READ | PROT_WRITE);
	//printf("pthread_exit writenvm %d\n",addr);
	return 0;
}

static size_t fuckfs_pwrites(void *arg){
	struct write_arg *warg;
	warg = (struct write_arg *)arg;
	struct  inode *inode = warg->inode;
	uint8_t *buf = warg->buf;
	offset_t off = warg->off;
	size_t size = warg->size;
	offset_t addr = warg->addr;
	userfs_write_ssd(inode->devid,buf,addr,size);
	printf("pthread_exit writessd %d\n",addr);
	return 0;
}
*/

/*
static size_t fuckfs_pwrite(void *arg){
	struct write_arg *warg;
	warg = (struct write_arg *)arg;
	struct  inode *inode = warg->inode;
	uint8_t *buf = warg->buf;
	offset_t off = warg->off;
	size_t size = warg->size;
	*/
/*
	//userfs_info("fuckfs_pwrite %d\n",size);
	struct fuckfs_extent * fsextent;
	struct timeval curtime;
	//int isnvm;
	fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));
	unsigned long addr;
	unsigned long n = size;
	struct space *space = (struct space *)malloc(sizeof(struct space));
	struct space *tmpspace;
	struct list_head *pos,*node;
	struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));
*/
/*
	uint32_t blocknum = (uint32_t)ceil((float)size/(float)g_block_size_bytes);
	unsigned long n = size;
	//unsigned long blocknum = n >> g_block_size_shift;
	//pthread_mutex_lock(&mutex);
	printf("pthread isnvm %d\n",isnvm);
	if(isnvm<10){
		printf("write to nvm\n");
		//pthread_mutex_lock(&mutex);
		memcpy(mmapstart+nvmstartaddr, buf,n);
		
		nvmstartaddr+=size;
		//pthread_mutex_unlock(&mutex);
	}else{
		printf("write to ssd %d\n",n);
		*/
		/*
		struct buffer_head *bh;
		bh=bh_get_sync_IO(2, ssdstartaddr, BH_NO_DATA_ALLOC);
		bh->b_data= buf;
		bh->b_size = (((n-1)/4096)+1)*4096;
		//printf("bh->b_size %d\n", bh->b_size);
		ssdstartaddr+=n/4096;
		userfs_write(bh);
		//userfs_assert(!ret);
		bh_release(bh);
		*/

		//pthread_mutex_lock(&mutex);
/*
		printf("writeblkaddr %d\n", ssdstartaddr/4096);
		userfs_write_ssd(inode->devid,buf,ssdstartaddr,n);
		ssdstartaddr+=n;
		//ssdstartaddr+=n/4096;
		//pthread_mutex_unlock(&mutex);
	}
	printf("write complete\n");
	pthread_mutex_lock(&mutex);
	isnvm++;
	isnvm=isnvm%10;
	pthread_mutex_unlock(&mutex);
	//return size;
//	size_t remain = size - n;
//	pthread_exit((void*)remain);
	printf("pthread_exit isnvm %d\n",isnvm);
	return (size-n);
}
*/
/*	INIT_LIST_HEAD(&(space->list));    
	isnvm = findspace(inode,blocknum,space);    //得到的space是指一种设备上的空间范围，何种设备取决于isnvm

	
	//printf("isnvm is %d\n",isnvm);
	if(likely(isnvm>=0)){
	//	panic("error allocate_space\n");
	//}else{ //判断写入哪个设备
	//	memcpy(mmapstart+startaddr, buf,n);
	//}
	//return size;
		userfs_info("strat write %d\n",size);
		uint32_t offset=0;
		int ret;
		list_for_each_safe(pos,node, &(space->list))
		{
			//printf("in list_for_each\n");
			fsextent->ext_block = off;
			blocknode->fileoff = off;
			tmpspace=list_entry(pos, struct space, list);
			if(!tmpspace)
				panic("find space error\n");
			//printf("tmpspace->startaddr %d -------- %d\n",tmpspace->startaddr, tmpspace->endaddr);
			//printf("tmpspace->size %d\n",tmpspace->size);

			unsigned long spacesize = tmpspace->size<<g_block_size_shift;
			addr_t block_nr = tmpspace->startaddr;//) >> g_block_size_shift;
		 	/*
		 	struct fcache_block *fc_block;
			//printf("fcache_find\n");
			fc_block = fcache_find(inode, off);
			if (!fc_block)
				fc_block = fcache_alloc_add(inode, off, block_nr);
			//printf("end fcache_find\n");
			*/
/*
			if(unlikely(spacesize > n)){
				if(isnvm ==0){
					inode->usednvms+=tmpspace->size;
					//printf("%d write NVM block_nr %lu\n",inode->inum, tmpspace->startaddr);
					addr=(tmpspace->startaddr) << g_block_size_shift;
					//printf("byte addr %ld, wirte size %d\n", addr,n);
					memcpy(mmapstart+addr, buf+offset,n);

					fsextent->device_id = 1;
					blocknode->devid = 1;
					//uniaddr = (tmpspace->startaddr) >> g_block_size_shift;//块地址

				}else{
					printf("%d write SSD block_nr %lu\n",inode->inum, tmpspace->startaddr);
					addr_t block_nr = tmpspace->startaddr;
					userfs_direct_write(buf,block_nr,n);
					/*
					struct buffer_head *bh;
					
					bh=bh_get_sync_IO(2, block_nr, BH_NO_DATA_ALLOC);
					bh->b_data= buf;
					int nsize = ((n-1)/g_block_size_bytes+1)*g_block_size_bytes;
					//printf("nsize1 is %d\n", nsize);
					bh->b_size = nsize;
					
					ret = userfs_write(bh);
					userfs_assert(!ret);
					bh_release(bh);
					*/
/*					block_nr = block_nr + NVMBLOCK;
					fsextent->device_id = 2;
					blocknode->devid = 2;
					writenum++;
					/*
					fc_block->is_data_cached = 1;
					fc_block->data =buf;
					*/
/*				}
				userfs_info("write success2 %d\n",n);
				blocknode->size =n;
				offset+=n;
				off=off+ n;
				fsextent->ext_len = n;
				n=0;
			}else{
				//memcpy(mmapstart+tmpspace->startaddr, buf+offset,tmpspace->size);
				if(isnvm ==0){
					//printf("%d write NVM block_nr %lu\n",inode->inum, tmpspace->startaddr);
					inode->usednvms+=tmpspace->size;
					addr=(tmpspace->startaddr) << g_block_size_shift;
					memcpy(mmapstart+addr, buf+offset,spacesize);
					fsextent->device_id = 1;
					blocknode->devid = 1;
					//uniaddr = tmpspace->startaddr;
				}else{
					printf("%d write SSD block_nr %lu\n",inode->inum, tmpspace->startaddr);
					addr_t block_nr = tmpspace->startaddr;
					userfs_direct_write(buf,block_nr,spacesize);
					/*
					struct buffer_head *bh;
					//addr_t block_nr = (tmpspace->startaddr) >> g_block_size_shift;
					bh=bh_get_sync_IO(2, block_nr, BH_NO_DATA_ALLOC);
					bh->b_data= buf;
					bh->b_size = spacesize >>g_block_size_shift;;
					//printf("spacesize is %d\n", spacesize);
					ret = userfs_write(bh);
		
					userfs_assert(!ret);
					
					bh_release(bh);

					*/
/*					fsextent->device_id = 2;
					blocknode->devid = 2;
					
					writenum++;

					/*
					fc_block->is_data_cached = 1;
					fc_block->data =buf;
					*/
/*					block_nr = block_nr + NVMBLOCK;

				}
				userfs_info("write success2 %d\n",n);
				n=n-spacesize;
				offset+=spacesize;
				off=off+ spacesize;
				fsextent->ext_len = spacesize;
				list_del(pos);

			}

//			fc_block->log_addr = block_nr;
			//fsextent->entry_type=FILE_WRITE;
			//printf("block_nr is %d\n",block_nr);
			fsextent->ext_start_hi = block_nr>>32;
			fsextent->ext_start_lo = block_nr;
			//printf("high is %d, low is %d\n", fsextent->ext_start_hi, fsextent->ext_start_lo);
			fsextent->ino = inode->inum;
			fsextent->isappend = 1;
			fsextent->entry_type = FILE_WRITE; //FILE_WRITE
			gettimeofday(&curtime,NULL);
			fsextent-> mtime =curtime.tv_sec;
			userfs_info("append_entry_log %d\n",inode->inum);
			append_entry_log(fsextent,0);
			
		
			blocknode->addr = tmpspace->startaddr;
			userfs_info("block_insert %d\n",writenum);
			//block_insert(&(inode->blktree),blocknode);

			userfs_info("write n is %d\n", n);
			
			if(n==0)
				break;
			
			//offset_t key;
			//fc_block->log_addr = ;
			//key = (loghdr->data[idx] >> g_block_size_shift);
			//fcache_alloc_add(inode, key, logblk_no);
			//add_to_read_cache(inode)
		}
		//初始化extent
	}else{
		panic("can not find space!!");
	}
	//else{

	/*	struct buffer_head *bh;
		bh=bh_get_sync_IO(uint8_t dev, block_nr, BH_NO_DATA_ALLOC);
		bh->data= buf;
		bh->b_size = n;
		bh->b_size = n;
		bh->b_offset = 0;
		ret = userfs_write(bh_data);
		userfs_assert(!ret);
		clear_buffer_uptodate(bh_data);
		bh_release(bh_data);
		fsextent->extent.ee_len = n;
		fsextent->extent.ee_block;
		fsextent->extent.ee_start_hi;
		fsextent->extent.ee_start_lo;
		fextent->device_id = dev;
		//初始化extent
		*/
	//}
/*	userfs_info("size is %d, n is %d, return %d\n",size,n,size-n);
	size_t remain = size - n;
	pthread_exit((void*)remain);
	return (size-n);
	
}*/

//小于等于4K的写
/*
static size_t fuckfs_write(struct inode *inode, uint8_t *buf, offset_t off, size_t size){
	userfs_info("fuckfs_write %d\n",size);
	struct fuckfs_extent * fsextent;
	struct timeval curtime;
	int isnvm;
	fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));
	unsigned long addr;
	unsigned long n = size;
	struct space *space = (struct space *)malloc(sizeof(struct space));
	struct space *tmpspace;
	struct list_head *pos,*node;
	struct block_node *blocknode =(struct block_node *)malloc(sizeof(struct block_node));

	uint32_t blocknum = (uint32_t)ceil((float)size/(float)g_block_size_bytes);

	//unsigned long blocknum = n >> g_block_size_shift;
/*	if(isnvm<8){
		//printf("write to nvm\n");
		memcpy(mmapstart+nvmstartaddr, buf,n);
		nvmstartaddr+=size;
	}else{
		//printf("write to ssd %d\n",n);
		struct buffer_head *bh;
		bh=bh_get_sync_IO(2, ssdstartaddr, BH_NO_DATA_ALLOC);
		bh->b_data= buf;
		bh->b_size = (((n-1)/4096)+1)*4096;
		//printf("bh->b_size %d\n", bh->b_size);
		ssdstartaddr+=n/4096;
		userfs_write(bh);
		//userfs_assert(!ret);
		bh_release(bh);
	}
	isnvm++;
	isnvm=isnvm%10;
	return size;
	*/
/*	INIT_LIST_HEAD(&(space->list));    
	isnvm = findspace(inode,blocknum,space);    //得到的space是指一种设备上的空间范围，何种设备取决于isnvm

	
	//printf("isnvm is %d\n",isnvm);
	if(likely(isnvm>=0)){
	//	panic("error allocate_space\n");
	//}else{ //判断写入哪个设备
	//	memcpy(mmapstart+startaddr, buf,n);
	//}
	//return size;
		userfs_info("strat write %d\n",size);
		uint32_t offset=0;
		int ret;
		list_for_each_safe(pos,node, &(space->list))
		{
			//printf("in list_for_each\n");
			fsextent->ext_block = off;
			blocknode->fileoff = off;
			tmpspace=list_entry(pos, struct space, list);
			if(!tmpspace)
				panic("find space error\n");
			//printf("tmpspace->startaddr %d -------- %d\n",tmpspace->startaddr, tmpspace->endaddr);
			//printf("tmpspace->size %d\n",tmpspace->size);

			unsigned long spacesize = tmpspace->size<<g_block_size_shift;
			addr_t block_nr = tmpspace->startaddr;//) >> g_block_size_shift;
/*			struct fcache_block *fc_block;
			//printf("fcache_find\n");
			fc_block = fcache_find(inode, off);
			if (!fc_block)
				fc_block = fcache_alloc_add(inode, off, block_nr);
			//printf("end fcache_find\n");
*/

/*			if(unlikely(spacesize > n)){
				if(isnvm ==0){
					inode->usednvms+=tmpspace->size;
					//printf("%d write NVM block_nr %lu\n",inode->inum, tmpspace->startaddr);
					addr=(tmpspace->startaddr) << g_block_size_shift;
					//printf("byte addr %ld, wirte size %d\n", addr,n);
					memcpy(mmapstart+addr, buf+offset,n);

					fsextent->device_id = 1;
					blocknode->devid = 1;
					//uniaddr = (tmpspace->startaddr) >> g_block_size_shift;//块地址

				}else{
					//printf("%d write SSD block_nr %lu\n",inode->inum, tmpspace->startaddr);
					//addr_t block_nr = tmpspace->startaddr;
					///userfs_direct_write(buf,block_nr,n);
					struct buffer_head *bh;
					
					bh=bh_get_sync_IO(2, block_nr, BH_NO_DATA_ALLOC);
					bh->b_data= buf;
					int nsize = ((n-1)/g_block_size_bytes+1)*g_block_size_bytes;
					//printf("nsize1 is %d\n", nsize);
					bh->b_size = nsize;
					
					ret = userfs_write(bh);
					userfs_assert(!ret);
					bh_release(bh);
					
					block_nr = block_nr + NVMBLOCK;
					fsextent->device_id = 2;
					blocknode->devid = 2;
					writenum++;
/*
					fc_block->is_data_cached = 1;
					fc_block->data =buf;
*/
/*				}
				userfs_info("write success2 %d\n",n);
				blocknode->size =n;
				offset+=n;
				off=off+ n;
				fsextent->ext_len = n;
				n=0;
			}else{
				//memcpy(mmapstart+tmpspace->startaddr, buf+offset,tmpspace->size);
				if(isnvm ==0){
					//printf("%d write NVM block_nr %lu\n",inode->inum, tmpspace->startaddr);
					inode->usednvms+=tmpspace->size;
					addr=(tmpspace->startaddr) << g_block_size_shift;
					memcpy(mmapstart+addr, buf+offset,spacesize);
					fsextent->device_id = 1;
					blocknode->devid = 1;
					//uniaddr = tmpspace->startaddr;
				}else{
					//printf("%d write SSD block_nr %lu\n",inode->inum, tmpspace->startaddr);
					//addr_t block_nr = tmpspace->startaddr;
					//userfs_direct_write(buf,block_nr,spacesize);
					
					struct buffer_head *bh;
					//addr_t block_nr = (tmpspace->startaddr) >> g_block_size_shift;
					bh=bh_get_sync_IO(2, block_nr, BH_NO_DATA_ALLOC);
					bh->b_data= buf;
					bh->b_size = spacesize >>g_block_size_shift;
					//printf("spacesize is %d\n", spacesize);
					ret = userfs_write(bh);
		
					userfs_assert(!ret);
					
					bh_release(bh);
					fsextent->device_id = 2;
					blocknode->devid = 2;
					
					writenum++;

/*					fc_block->is_data_cached = 1;
					fc_block->data =buf;
*/
/*					block_nr = block_nr + NVMBLOCK;

				}
				userfs_info("write success2 %d\n",n);
				n=n-spacesize;
				offset+=spacesize;
				off=off+ spacesize;
				fsextent->ext_len = spacesize;
				list_del(pos);

			}

//			fc_block->log_addr = block_nr;
			//fsextent->entry_type=FILE_WRITE;
			//printf("block_nr is %d\n",block_nr);

			// block_nr 块类型nvm or ssd, 大小
			fsextent->ext_start_hi = block_nr>>32;
			fsextent->ext_start_lo = block_nr;
			//printf("high is %d, low is %d\n", fsextent->ext_start_hi, fsextent->ext_start_lo);
			fsextent->ino = inode->inum;
			fsextent->isappend = 1;
			fsextent->entry_type = FILE_WRITE; //FILE_WRITE
			gettimeofday(&curtime,NULL);
			fsextent-> mtime =curtime.tv_sec;
			userfs_info("append_entry_log %d\n",inode->inum);
			append_entry_log(fsextent,0);
			
		
			blocknode->addr = tmpspace->startaddr;;
			userfs_info("block_insert %d\n",writenum);
			pthread_rwlock_wrlock(&inode->blktree_rwlock);
			block_insert(&(inode->blktree),blocknode);
			pthread_rwlock_unlock(&inode->blktree_rwlock);

			userfs_info("write n is %d\n", n);
			
			if(n==0)
				break;
			
			//offset_t key;
			//fc_block->log_addr = ;
			//key = (loghdr->data[idx] >> g_block_size_shift);
			//fcache_alloc_add(inode, key, logblk_no);
			//add_to_read_cache(inode)
		}
		//初始化extent
	}else{
		panic("can not find space!!");
	}
	//else{

	/*	struct buffer_head *bh;
		bh=bh_get_sync_IO(uint8_t dev, block_nr, BH_NO_DATA_ALLOC);
		bh->data= buf;
		bh->b_size = n;
		bh->b_size = n;
		bh->b_offset = 0;
		ret = userfs_write(bh_data);
		userfs_assert(!ret);
		clear_buffer_uptodate(bh_data);
		bh_release(bh_data);
		fsextent->extent.ee_len = n;
		fsextent->extent.ee_block;
		fsextent->extent.ee_start_hi;
		fsextent->extent.ee_start_lo;
		fextent->device_id = dev;
		//初始化extent
		*/
	//}
	//userfs_info("size is %d, n is %d, return %d\n",size,n,size-n);
/*	return (size-n);
}
*/

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
	//printf("nvmwrite fileno %d, off %d\n", ip->fileno,off);
	//printf("nvmwrite nvmoff %d, off %d\n", ip->nvmoff,off);
	//userfs_info("fs_nvmwrite inode %lu, filesize %d, off %d, size %d\n",ip, ip->size, off, size);
	struct buffer_head *bh;
	struct fuckfs_extent * fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));;
	struct timeval curtime;
	unsigned long writeoff, fileoff;
	uint32_t writesize=size;
	//int isnvm;
	uint64_t allosize;
	//printf("nvm block %d\n", ip->nvmblock);
	if(ip->nvmblock==0){
		
		allosize=allocatenvmblock(FILENVMBLOCK - ip->nvmblock,ip);  //字節地址
		//printf("ip->nvmoff %ld\n", ip->nvmoff);
		if(allosize==0)
			return -1;
		
		//printf("off&(1<<g_block_size_shift -1) %d\n", off&((1<<g_lblock_size_shift) -1));
		ip->nvmblock = FILENVMBLOCK;
		ip->nvmoff+=off&((FILENVMBLOCK<<g_block_size_shift) -1);
	}

	//printf("ip->nvmoff %ld, >>12 %d\n", ip->nvmoff,ip->nvmoff>>12);
	writeoff= ip->nvmoff >>g_block_size_shift;
	//printf("writeoff %d\n", writeoff);
	bh = bh_get_sync_IO(1,ip->nvmoff>>12,BH_NO_DATA_ALLOC);
	bh->b_data=buf;
	bh->b_size=size;
	bh->b_offset = off&((1<<g_block_size_shift) -1);

	userfs_write(bh);
	//threadpool_add_job(wrthreadpool, userfs_write, (void *) bh);

	
	//char * writeaddr=ip->writespace+off;
	
	//memcpy(writeaddr, buf, size);

	//printf("size %d\n", size);
	fsextent->device_id = 0;

	
	fsextent->ino = ip->inum;
	fsextent->ext_start_hi = (ip->nvmoff)>>32;  //字節地址
	fsextent->ext_start_lo = ip->nvmoff;
	fsextent->ext_len = size + (((uint32_t) 0x1<<g_block_size_shift) -1) >>g_block_size_shift;
	//printf("fsextent->ext_len %d\n", fsextent->ext_len);
	fsextent->ext_block = off >> g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	ip->nvmoff +=size;
	fsextent->blkoff_hi = ip->nvmoff>>32;
	fsextent->blkoff_lo = ip->nvmoff;
	//userfs_info("fsextent->blkoff_hi %ld,fsextent->blkoff_lo %ld\n",fsextent->blkoff_hi,fsextent->blkoff_lo);
	//userfs_info("append_entry_log %d\n",ip->inum);
	append_entry_log(fsextent,0);


	//printf("(~((uint64_t)0x1<<g_block_size_shift -1)) %lu\n", ~(((uint64_t)0x1<<g_block_size_shift) -1));
	fileoff = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	
	while(writesize >0){
		//printf("off %d, fileoff %d\n", off,fileoff);
		//userfs_info("inode %lu,writesize %d, fileoff %d\n",ip,writesize,fileoff);
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		struct block_node *blocknode =block_search(&(ip->blktree),fileoff);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		//userfs_info("inode %lu, blocknode %lu\n", ip,blocknode);
		if(!blocknode){ //NULL
			blocknode=(struct block_node *)malloc(sizeof(struct block_node));
			//blocknode = block_search(&(ip->blktree),_off);
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
			//userfs_info("inode %lu, blocknode->size %d\n",ip,blocknode->size);
			pthread_rwlock_wrlock(&ip->blktree_rwlock);
			block_insert(&(ip->blktree),blocknode);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}else{//not NULL
			//userfs_info("inode %lu, 0blocknode->size %d\n",ip,blocknode->size);
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
			//userfs_info("inode %lu, 1blocknode->size %d\n",ip, blocknode->size);

		}
		writeoff++;
		fileoff+=g_block_size_bytes;
		//printf("blocknode->addr %d\n", blocknode->addr);
		//printf("blocknode->fileoff %d\n",blocknode->fileoff);
		//printf("blocknode->size %d\n", blocknode->size);
	}
	bh_release(bh);
	//userfs_info("nvm write success %d\n",size);
	return size;
}



uint64_t allocate_ssdblock(uint8_t devid){
	//printf("allocate_ssdblock devid %d\n", devid);
	//printf("ADDRINFO->ssd1_block_addr %ld, ADDRINFO->ssd2_block_addr %ld, ADDRINFO->ssd3_block_addr %ld\n", ADDRINFO->ssd1_block_addr,ADDRINFO->ssd2_block_addr,ADDRINFO->ssd3_block_addr);
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
	//userfs_info("fs_ssdwrite inode %lu, filesize %d, off %d, size %d\n",ip, ip->size, off, size);
	//printf("ssdwrite fileno %d devid %d\n", ip->fileno, devid);
	//printf("ssdwrite ip->ssdoff %ld\n",ip->ssdoff);
	struct buffer_head *bh;

	struct fuckfs_extent * fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));;
	struct timeval curtime;
	//int isnvm;
	unsigned long writeoff, fileoff;
	uint32_t writesize=size;
	offset_t off_aligned_2M = off &(~(((uint64_t)0x1<<g_lblock_size_shift) -1));
/*
	if(devid<=0&&(ip->ssdoff<=0)){
		ip->ssdoff=allocate_ssdblock(devid);
	}else if((ip->ssdoff<<43)==0){
		ip->devid=(devid+1)%3;
		ip->ssdoff=allocate_ssdblock(ip->devid);
	}
	*/
	//printf("0ip->ssdoff %d\n",ip->ssdoff);
	//printf("fileoff %d\n", off);
	if((ip->ssdoff<<43)==0 || ip->ssdoff >= ssd_size[ip->devid]){
		//ip->devid=(devid+1)%3;
		ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
		//printf("off&(1<<g_block_size_shift -1) %d\n", off&((1<<g_lblock_size_shift) -1));
		//printf("ip->ssdoff %ld\n",ip->ssdoff);
		ip->ssdoff+=off&((1<<g_lblock_size_shift) -1);
	}
	//printf("ip->ssdoff %lu\n",ip->ssdoff);
	writeoff= ip->ssdoff >>g_block_size_shift;
	//printf("ip->ssdoff %lu, writeoff %d\n",ip->ssdoff,writeoff);
	bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
	bh->b_data=buf;
	bh->b_size=size;
	bh->b_offset=off&((1<<g_block_size_shift) -1);
	//printf("ssdwrite devid %d ssdoff %d, bh->b_offset %d\n", ip->devid, ip->ssdoff>>12,bh->b_offset);
	userfs_write(bh);
	//userfs_write_ssd(ip->devid,ip->ssdoff, buf,size);
	
	bh_release(bh);

	fsextent->device_id = ip->devid;

	fsextent->ino = ip->inum;
	fsextent->ext_start_hi = ip->ssdoff>>32; //字節地址
	fsextent->ext_start_lo = ip->ssdoff;
	fsextent->ext_block = off >>g_block_size_shift;
	//printf("ext_start_hi %d, ext_start_lo %d\n",fsextent->ext_start_hi,fsextent->ext_start_lo);
	fsextent->ext_len = size + (((uint32_t) 0x1<<g_block_size_shift) -1) >>g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	ip->ssdoff+=size;
	fsextent->blkoff_hi = ip->ssdoff>>32;
	fsextent->blkoff_lo = ip->ssdoff;
	//userfs_info("fsextent->blkoff_hi %ld,fsextent->blkoff_lo %ld\n",fsextent->blkoff_hi,fsextent->blkoff_lo);
	//userfs_info("append_entry_log %d\n",ip->inum);
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
			//printf("fileoff %d\n", fileoff);
			//userfs_info("inode %lu, writesize %d, fileoff %d\n",ip, writesize,fileoff);
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
				//userfs_info("inode %lu,blocknode->size %d\n",ip,blocknode->size);
				blocknode->addr= writeoff;
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				block_insert(&(ip->blktree),blocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
			}else{//not NULL
				//userfs_info("inode %lu,0blocknode->size %d\n",ip,blocknode->size);
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
				//userfs_info("inode %lu,1blocknode->size %d\n",ip,blocknode->size);
			}
			writeoff++;
			fileoff+=g_block_size_bytes;
			//printf("blocknode->size %d\n", blocknode->size);
			//printf("blocknode->addr %d\n", blocknode->addr);
			//printf("blocknode->fileoff %d\n",blocknode->fileoff);
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
	//bmap_req.blk_count_found = 0;
	//bmap_req.blk_count = 1;
	userfs_info("off_aligned_4K %d,off_aligned_2M %d\n",off_aligned_4K,off_aligned_2M);
	if(!blocknode){ //4K NULL
		pthread_rwlock_rdlock(&ip->blktree_rwlock);
		blocknode =block_search(&(ip->blktree),off_aligned_2M);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		if(!blocknode ||(blocknode->size+blocknode->fileoff)< off){ //2M NULL
			//blocknode=(struct block_node *)malloc(sizeof(struct block_node));
			bmap_req.start_offset = off_aligned_4K;
			int ret=bmap(ip,&bmap_req);
			//userfs_info("bmap ret %d\n",ret);
			userfs_info("bmap_req.start_offset %d\n",bmap_req.start_offset);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
			//userfs_info("blocknode %lu\n",blocknode);
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
	//userfs_info("writeoff %ld, ip->nvmoff %ld\n",writeoff,ip->nvmoff);
	writeoff += blockno<<g_block_size_shift;
	fsextent->ino = ip->inum;
	fsextent->ext_start_hi = (writeoff)>>32; //字節地址
	fsextent->ext_start_lo = (writeoff);
	fsextent->ext_block = off >>g_block_size_shift;
	//printf("ext_start_hi %d, ext_start_lo %d\n",fsextent->ext_start_hi,fsextent->ext_start_lo);
	fsextent->ext_len = size + (((uint32_t) 0x1<<g_block_size_shift) -1) >>g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	//offset_t writeoff = blockno<<g_block_size_shift + bh->b_offset;
	//userfs_info("writeoff %ld, ip->nvmoff %ld\n",writeoff,ip->nvmoff);
	//if(writeoff==ip->nvmoff && bmap_req.dev ==1){
	if((off+size)>ip->size && blocknode->devid ==1){
		ip->nvmoff += size;
		fsextent->blkoff_hi = (ip->nvmoff)>>32;
		fsextent->blkoff_lo = (ip->nvmoff);
	//}else if(writeoff==ip->ssdoff && bmap_req.dev > 1){
	}else if((off+size)>ip->size && blocknode->devid > 1){
		ip->ssdoff += size;
		fsextent->blkoff_hi = (ip->ssdoff)>>32;
		fsextent->blkoff_lo = (ip->ssdoff);
	}else{
		fsextent->blkoff_hi = 0;
		fsextent->blkoff_lo = 0;
	}
	

	//fsextent->blkoff_hi = ip->ssdoff>>32;
	//fsextent->blkoff_lo = ip->ssdoff;
	//userfs_info("fsextent->blkoff_hi %ld,fsextent->blkoff_lo %ld\n",fsextent->blkoff_hi,fsextent->blkoff_lo);
	//userfs_info("append_entry_log %d\n",ip->inum);
	append_entry_log(fsextent,0);
	blocknode->size = off + size - blocknode->fileoff;
	//userfs_info("blocknode->size %d\n",blocknode->size);
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
	//userfs_info("ext_start_lo %d\n",fsextent->ext_start_lo);
	fsextent->ext_start_lo += off &(((uint64_t)0x1<<g_block_size_shift) -1);
	//userfs_info("ext_start_lo %d\n",fsextent->ext_start_lo);
	//userfs_info("off %d\n",off);
	fsextent->ext_block = off >>g_block_size_shift;
	//userfs_info("fsextent->ext_block %d\n", fsextent->ext_block);
	//printf("ext_start_hi %d, ext_start_lo %d\n",fsextent->ext_start_hi,fsextent->ext_start_lo);
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
	//struct fuckfs_extent *fsextent = (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));
	struct timeval curtime;
	if(devid ==1){ //nvm地址
		//if(ip->nvmoff == off && (off+size) <= (FILENVMBLOCK<<g_block_size_shift)){
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
				//userfs_info("inode %lu, newblocknode->size %d\n",ip,blocknode->size);
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
			//printf("ssdwrite devid %d ssdoff %d, bh->b_offset %d\n", ip->devid, ip->ssdoff>>12,bh->b_offset);
			userfs_write(bh);
			//userfs_write_ssd(ip->devid,ip->ssdoff, buf,size);
			

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
				//userfs_info("inode %lu, newblocknode->size %d, writesize %d\n",ip,blocknode->size,writesize);
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
		//userfs_write_ssd(ip->devid,ip->ssdoff, buf,size);


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
	/*
	fsextent->ino = ip->inum;
	fsextent->device_id = bh->b_dev;
	fsextent->ext_start_hi = bh->b_blocknr>>32; //字節地址
	fsextent->ext_start_lo = bh->b_blocknr;
	fsextent->ext_block = off >>g_block_size_shift;
	//printf("ext_start_hi %d, ext_start_lo %d\n",fsextent->ext_start_hi,fsextent->ext_start_lo);
	fsextent->ext_len = size >> g_block_size_shift;
	fsextent->size =size;
	fsextent->isappend = 1;
	fsextent->entry_type = FILE_WRITE; //FILE_WRITE
	gettimeofday(&curtime,NULL);
	fsextent-> mtime =curtime.tv_sec;
	//fsextent->blkoff_hi = ;
	//fsextent->blkoff_lo
	//
	*/
	//userfs_info("append_write ret size %d\n",size);
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
		//if (size > 0 && (off + size) > ip->size) 
		//	ip->size = off + size;
		return retsize;
	}else{ //in-place update &/ append write
		//找到off对应的地址，写数据
		//如果是2M ssd地址，直接覆盖
		//如果是nvm地址，循环覆盖
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
		//struct fuckfs_extent * fsextent;//= (struct fuckfs_extent *)malloc(sizeof(struct fuckfs_extent));;
		//struct timeval curtime;
		
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
				/*
				fsextent->ino = ip->inum;
				fsextent->device_id = bh->b_dev;
				fsextent->ext_start_hi = bh->b_blocknr>>32; //字節地址
				fsextent->ext_start_lo = bh->b_blocknr;
				fsextent->ext_block = off >>g_block_size_shift;
				//printf("ext_start_hi %d, ext_start_lo %d\n",fsextent->ext_start_hi,fsextent->ext_start_lo);
				fsextent->ext_len = size >> g_block_size_shift;
				fsextent->size =size;
				fsextent->isappend = 1;
				fsextent->entry_type = FILE_WRITE; //FILE_WRITE
				gettimeofday(&curtime,NULL);
				fsextent-> mtime =curtime.tv_sec;
				//fsextent->blkoff_hi = ;
				//fsextent->blkoff_lo
				*/
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
				
				/*
				fsextent->ino = ip->inum;
				fsextent->device_id = bh->b_dev;
				fsextent->ext_start_hi = bh->b_blocknr>>32; //字節地址
				fsextent->ext_start_lo = bh->b_blocknr;
				fsextent->ext_block = off >>g_block_size_shift;
				//printf("ext_start_hi %d, ext_start_lo %d\n",fsextent->ext_start_hi,fsextent->ext_start_lo);
				fsextent->ext_len = size >> g_block_size_shift;
				fsextent->size =size;
				fsextent->isappend = 1;
				fsextent->entry_type = FILE_WRITE; //FILE_WRITE
				gettimeofday(&curtime,NULL);
				fsextent-> mtime =curtime.tv_sec;
				//fsextent->blkoff_hi = ;
				//fsextent->blkoff_lo
				*/
			}else{
				//分配空间
				//===bug
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
			//userfs_info("blocknode->devid %d, blocknode->addr %d, onewsize %d\n",blocknode->devid,blocknode->addr,onewsize);
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


//如果部分in-place update,部分追加写怎么办
int aligned_4K_write(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){
	//userfs_info("aligned_4K_write size %d, off %d, ip->size %d\n",size, off,ip->size);
	int retsize;
	//offset_t off_aligned_4K_pre = off - g_block_size_bytes;
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
			//userfs_info("bmap ret %d\n",ret);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}
		//if(blocknode->size >(off+size-off_aligned_2M)){
		if(blocknode->devid>1){//如果是ssd地址
			bh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off-off_aligned_2M)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			bh->b_data=buf;
			bh->b_size=size;

			bh->b_offset=0;
			//userfs_info("addr %d ,devid %d\n",blocknode->addr+((off-off_aligned_2M)>>g_block_size_shift), blocknode->devid);
			userfs_write(bh);
			//bh_release(bh);
			if(blocknode->size <(off+size-off_aligned_2M)){
				blocknode->size =(off+size-off_aligned_2M);
				//userfs_info("blocknode->size %d\n",blocknode->size);
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
					//userfs_info("bmap ret %d\n",ret);
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
						//userfs_write_ssd(ip->devid,ip->ssdoff, buf,size);
						//bh_release(bh);

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
		//找到off对应的地址，写数据
		//找到off_2M对应的地址，是否是2M的ssd块
		//如果是4k的块，循环覆盖

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

		
		/*uint32_t remainsize = size,onewsize,writesize=0;
		//bmap_req_t bmap_req;
		//struct block_node *blocknode;
		//struct buffer_head *bh;
		while(remainsize > 0){
			blocknode =block_search(&(ip->blktree),off);
			if(!blocknode){//NULL
				bmap_req.start_offset=off;
				bmap(ip,&bmap_req);
				blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			}
			if(blocknode->fileoff == off_aligned_2M){ //在2M块中写
				onewsize = remainsize< blocknode->size? remainsize:blocknode->size;
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr +((off-off_aligned_2M)>>g_block_size_shift),BH_NO_DATA_ALLOC);
				bh->b_data=buf+writesize;
				bh->b_size=onewsize;

				bh->b_offset=0;
				userfs_write(bh);
				writesize+=onewsize;
				off+=onewsize;
				remainsize-=onewsize;
				if(blocknode->size < (off-off_aligned_2M)){
					blocknode->size = (off-off_aligned_2M);
				}
			}else{ //4K写
				onewsize = remainsize< blocknode->size? remainsize:blocknode->size;
				bh=bh_get_sync_IO(blocknode->devid,blocknode->addr,BH_NO_DATA_ALLOC);
				bh->b_data=buf+writesize;
				bh->b_size=onewsize;

				bh->b_offset=0;
				userfs_write(bh);
				writesize+=onewsize;
				off+=onewsize;
				remainsize-=onewsize;
				if(blocknode->size < (off-off_aligned_4K)){
					blocknode->size = (off-off_aligned_4K);
				}
			}
		}*/
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
	userfs_info("inode %lu, blocknode %lu\n", ip,blocknode);
	if(!blocknode){ //NULL
		blocknode =(struct block_node *)malloc(sizeof(struct block_node));
		blocknode->devid =devid+1;
		blocknode->fileoff=off;
		blocknode->size=size;

		blocknode->addr= addr >>g_block_size_shift;
		userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
		pthread_rwlock_wrlock(&ip->blktree_rwlock);
		block_insert(&(ip->blktree),blocknode);
		pthread_rwlock_unlock(&ip->blktree_rwlock);
		return 1;		
	}else{
		userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
		offset_t bnodeoff= blocknode->size +blocknode->fileoff;
		offset_t writeoff= size+off;
		if(bnodeoff>writeoff){ //在其中
			struct block_node *afblocknode =(struct block_node *)malloc(sizeof(struct block_node));
			afblocknode->devid =blocknode->devid;
			afblocknode->fileoff=ALIGN(writeoff, g_block_size_bytes);
			afblocknode->size=bnodeoff - afblocknode->fileoff;

			afblocknode->addr=  blocknode->addr + ((afblocknode->fileoff - blocknode->fileoff)>>g_block_size_shift);
			userfs_info("inode %lu, afblocknode->size %d, afblocknode->fileoff %d,  afblocknode->addr %ld\n",ip,afblocknode->size,afblocknode->fileoff,afblocknode->addr);
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
				userfs_info("inode %lu, inblocknode->size %d, inblocknode->fileoff %d,  inblocknode->addr %ld\n",ip,inblocknode->size,inblocknode->fileoff,inblocknode->addr);
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				ret = block_insert(&(ip->blktree),inblocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
				userfs_info("ret %d\n", ret);
			}

				
			block_search(&(ip->blktree),blocknode->fileoff);
			//if(blocknode->size ==0){
			//	block_insert(&(ip->blktree),blocknode->fileoff);
			//}
			//userfs_info("inode %lu, blocknode->size %d, blocknode->fileoff %d,  blocknode->addr %ld\n",ip,blocknode->size,blocknode->fileoff,blocknode->addr);
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
				userfs_info("inode %lu, inblocknode->size %d, inblocknode->fileoff %d,  inblocknode->addr %ld\n",ip,inblocknode->size,inblocknode->fileoff,inblocknode->addr);
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				ret=block_insert(&(ip->blktree),inblocknode);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
				userfs_info("ret %d\n", ret);
				
			}
			block_search(&(ip->blktree),blocknode->fileoff);
			/*
			if(blocknode->size ==0){
				pthread_rwlock_wrlock(&ip->blktree_rwlock);
				block_delete(&(ip->blktree), blocknode->fileoff);
				//block_replace(&(ip->blktree),inblocknode, blocknode->fileoff);
				pthread_rwlock_unlock(&ip->blktree_rwlock);
			}
			*/
				
			//if(blocknode->size ==0){
			//	block_insert(&(ip->blktree),blocknode->fileoff);
			//}
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
	//userfs_info("blocknode %lu, off_aligned_4K %d\n",blocknode,off_aligned_4K);
	if(!blocknode){ //NULL
		bmap_req_t bmap_req;
		bmap_req.start_offset=off_aligned_4K;
		int ret=bmap(ip,&bmap_req);
		//printf("start_offset %d\n", off_aligned_4K);
		//userfs_info("bmap ret %d\n",ret);
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
			/*
			threadpool_add(writepool[bh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)bh);
#endif
			//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
			//ip->ssdoff+=size;
			blocknode->size += size;
			//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
			//insert to blocktree
		}else if(off<ip->size){   //in-place 更新
			char *readbuf = malloc(g_block_size_bytes);
			struct buffer_head *readbh;
			unsigned long addr;
			
			//ip->nvmoff=allocate_nvmblock(1);  //分配一个nvm块做log写
			//userfs_info("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, (ip->rwoff-ip->rwsoff)>>g_block_size_shift, ip->rwsoff);
			if(((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||((ip->rwoff-ip->rwsoff)>>g_block_size_shift)>=512))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					/*
					case 0:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->devid=0;
						break;
					*/
					
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
			//printf("bh_get_sync_IO\n");
			readbh->b_size=4096;
			readbh->b_offset =0;
			readbh->b_data = readbuf;
			//bh_submit_read_sync_IO(readbh);
			//printf("bh->b_data %s\n", bh->b_data);
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
			/*
			if(off_aligned_4K==blocknode->fileoff){ //blocknode表示这个4K块
				blocknode->size = off+size-off_aligned_4K;
				blocknode->devid =1;
				blocknode->addr =addr;
				blocknode->fileoff = off_aligned_4K;
			}else{
				//如果写的块是blocknode索引的连续块中间一个块的话，如何修改索引
				//upsert_blktree(ip, 1, addr, off_aligned_4K, off+size-off_aligned_4K);
			}
			*/
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
			/*
			threadpool_add(writepool[bh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)bh);
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

	/*
	if((ip->ssdoff<<43)==0){
		ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
	}
	bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
	*/
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
			//printf("start_offset %d\n", off);
			pthread_rwlock_rdlock(&ip->blktree_rwlock);
			blocknode =block_search(&(ip->blktree),bmap_req.start_offset);
			pthread_rwlock_unlock(&ip->blktree_rwlock);
		}
		//userfs_info("blocknode fileoff %d, addr %ld\n", blocknode->fileoff,blocknode->addr);
		if(blocknode &&((blocknode->fileoff+blocknode->size)>(off+size))){ //原有数据完全覆盖写的数据，读取此块后半部分数据，和要写的数据一起写入新的nvm块
			char *readbuf = malloc(g_block_size_bytes);
			struct buffer_head *readbh;
			readbh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off-blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			//printf("bh_get_sync_IO\n");
			uint32_t readsize = blocknode->fileoff+blocknode->size - off;
			if(readsize >=g_block_size_shift){
				readsize = g_block_size_bytes;
			}
			readbh->b_size = readsize;
			readbh->b_offset = 0;
			readbh->b_data = readbuf;
			//bh_submit_read_sync_IO(readbh);
			//printf("bh->b_data %s\n", bh->b_data);
			//bh_release(readbh);
			memcpy(readbuf,buf,size);  //4K块的前部分(buf)cp到readbuf

			//ip->nvmoff=allocate_nvmblock(1);
			//userfs_info("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, (ip->rwoff-ip->rwsoff)>>g_block_size_shift, ip->rwsoff);
			
			if(((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||((ip->rwoff-ip->rwsoff)>>g_block_size_shift)>=512))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					/*
					case 0:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->devid=0;
						break;
					*/
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
    		/*
			threadpool_add(writepool[bh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)bh);
#endif
			
			
			

			//如果写的块是blocknode索引的连续块中间一个块的话，如何修改索引
			

		}else if((blocknode->fileoff+blocknode->size)>(off+size)){   //块中数据小于写的数据
			//ip->nvmoff=allocate_nvmblock(1);
			//userfs_info("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, (ip->rwoff-ip->rwsoff)>>g_block_size_shift, ip->rwsoff);
			if(((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||((ip->rwoff-ip->rwsoff)>>g_block_size_shift)>=512))){	
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					/*
					case 0:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->devid=0;
						break;
					*/
					
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
			/*
			threadpool_add(writepool[bh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)bh);
#endif
			//blocknode->size = off - blocknode->fileoff; //原始索引对应的数据大小等于原始大小减去此块的原始大小
			
			 

			
		}
		return size;
	}
	
	//append 写
	
	//uint64_t devid = ip->devid;
	//if((ip->ssdoff<<43)==0){

	if(((ip->ssdoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->ssdoff ==0)||((ip->ssdoff-ip->usednvms)>>g_block_size_shift)>=512))){
		ip->wrthdnum = ip->wrthdnum % writethdnum;
		//userfs_info("ip->ino %d, ip->devid %d, ip->ssdoff %d, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
		switch(ip->wrthdnum){
			/*
			case 0:
				ip->nvmoff = allocate_nvmblock(512);
				ip->ssdoff = ip->nvmoff;
				ip->devid=0;
				break;
			*/
			
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
		//userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
		//ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
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
    /*
	threadpool_add(writepool[bh->b_dev], userfs_write, (void *)warg);
	for(int i=1;i<=(nvmnum+ssdnum);i++){
		printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
	}
	*/
	//threadpool_add(writethdpool, userfs_write, (void *)bh);
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
/*
	if((ip->ssdoff<<43)==0){
		ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
	}
*/
	
	//uint64_t writespace = ((ip->ssdoff + (((uint64_t)0x1<<g_lblock_size_shift)-1)) & ~((((uint64_t)0x1<<g_lblock_size_shift)-1))) -ip->ssdoff;
	//userfs_info("off %d, ip->size %d, ip->ssdoff %ld\n",off,ip->size,ip->ssdoff);
	//printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld, (ip->ssdoff-ip->usednvms)>>g_block_size_shift %d\n", ip->devid,ip->ssdoff,ip->ssdoff&0x1fffffUL,(ip->ssdoff-ip->usednvms)>>g_block_size_shift);
	if(off == ip->size){ //append写，直接写入后面   如果前半部分写到nvm，如何安置后半部分
		//printf("append write off %d\n", off);
		//if((ip->ssdoff<<43)==0){
		//userfs_printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld, (ip->ssdoff-ip->usednvms)>>g_block_size_shift %d\n", ip->devid,ip->ssdoff,ip->ssdoff&0x1fffffUL,(ip->ssdoff-ip->usednvms)>>g_block_size_shift);
		if(((ip->ssdoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->ssdoff ==0)||((ip->ssdoff-ip->usednvms)>>g_block_size_shift)>=512))){
			//userfs_printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld, (ip->ssdoff-ip->usednvms)>>g_block_size_shift %d\n", ip->devid,ip->ssdoff,ip->ssdoff&0x1fffffUL,(ip->ssdoff-ip->usednvms)>>g_block_size_shift);
			ip->wrthdnum = ip->wrthdnum % writethdnum;
			
			switch(ip->wrthdnum){
				/*
				case 0:
					ip->nvmoff = allocate_nvmblock(512);
					ip->ssdoff = ip->nvmoff;
					ip->devid=0;
					break;
				*/
			/*	
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
			*/	
				default:
					ip->nvmoff = allocate_nvmblock(512);
					ip->ssdoff = ip->nvmoff;
					ip->usednvms = ip->nvmoff ;
					ip->devid=0;
					//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
					break;
				
			/*
				default:
					ip->ssdoff = allocate_ssd1block();
					ip->devid=1;
					break;
				*/	
					
			}
			userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
			//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
			__sync_fetch_and_add(&ip->wrthdnum,1);
			//ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
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
		//printf("threadpool_write\n");
		/*pthread_mutex_t mutex;
		pthread_cond_t cond;
		if((pthread_mutex_init(&mutex, NULL) != 0) || (pthread_cond_init(&cond, NULL) != 0)) {
        	panic("pthread init error\n");
    	}
    	*/
    	//printf("wargument\n");
    	wargument *warg = (wargument *)malloc(sizeof(wargument));
    	warg->func_arg = abh;
    	//warg->mutex = mutex;
    	warg->wflag = 1;
    	warg->inode = ip;
    	warg->off = off;
    	warg->append =1;
    	//printf("abh->b_dev %d, %lu\n", abh->b_dev, writepool[abh->b_dev]);
		int ret = thpool_add_work(writepool[abh->b_dev], userfs_write, (void *)warg);
		if(ret < 0){
			printf("writepool[%d] queue_size %d\n",abh->b_dev,threadpool_queuesize(writepool[abh->b_dev]));
			panic("cannot add thread\n");
		}
		//for(int i=1;i<=(nvmnum+ssdnum);i++){
			//printf("writepool[%d] queue_size %d\n",i,threadpool_queuesize(writepool[i]));
		//}
		//threadpool_add(writethdpool, userfs_write, (void *)warg);
		
		//pthread_mutex_lock(&mutex);  
        //pthread_cond_wait(&warg->cond, &mutex);  
   		//pthread_mutex_unlock(&mutex);
   		
   		//threadpool_add(writethdpool, userfs_write, (void *)abh);
#endif
		
		
		//struct timeval start, end;
		//gettimeofday(&start, NULL);
   		//userfs_info("start sec %d, usec %d\n",start.tv_sec, start.tv_usec);
		//append_fsextent(ip,off,abh,1);
		//gettimeofday(&end, NULL);
    	//userfs_info("end   sec %d, usec %d\n",end.tv_sec, end.tv_usec);
    	//userfs_info("append_fsextent use time %d usec\n", end.tv_usec - start.tv_usec);
#ifndef CONCURRENT
		//bh_release(abh);
#endif
		//gettimeofday(&start, NULL);
   		//userfs_info("start sec %d, usec %d\n",start.tv_sec, start.tv_usec);
		//insert_blktree(ip,ip->devid+1, addr, off, size);
		//gettimeofday(&end, NULL);
    	//userfs_info("end   sec %d, usec %d\n",end.tv_sec, end.tv_usec);
    	//userfs_info("insert_blktree use time %d usec\n", end.tv_usec - start.tv_usec);

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
					/*
					case 0:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->devid=0;
						break;
					*/
					/*
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
						*/
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

    		/*threadpool_add(writepool[ibh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)warg);
			//threadpool_add(writethdpool, userfs_write, (void *)ibh);
#endif
			
			
			//append_fsextent(ip,off,ibh,1);
//#ifndef CONCURRENT
//			bh_release(ibh);
//#endif
			//老的索引此块的索引需不需要更新
			//upsert_blktree(ip, 1, addr, off, inplace);

			//append 写
			if(((ip->ssdoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->ssdoff ==0)||((ip->ssdoff-ip->usednvms)>>g_block_size_shift)>=512))){
				//userfs_printf("ip>devid %ld, ip->ssdoff %ld, ip->ssdoff<<43 %ld, (ip->ssdoff-ip->usednvms)>>g_block_size_shift %d\n", ip->devid,ip->ssdoff,ip->ssdoff&0x1fffffUL,(ip->ssdoff-ip->usednvms)>>g_block_size_shift);
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					/*
					case 0:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->devid=0;
						break;
					*/
					
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
				//userfs_printf("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
				//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->ssdoff %ld, ip->wrthdnum %d\n",ip->inum,ip->devid, ip->ssdoff,ip->wrthdnum);
				__sync_fetch_and_add(&ip->wrthdnum,1);
				//ip->ssdoff=allocate_ssdblock(ip->devid);  //字節地址
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
    		/*
			threadpool_add(writepool[abh->b_dev], userfs_write, (void *)warg1);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)abh);
#endif
			
			
			
			//append写直接插入到树中
			

		}else{  //完全inplace写
			userfs_info("inplace2 write off %d\n",off);
			//ip->nvmoff=allocate_nvmblock(size>>g_block_size_shift);
			//怎么分配ssd空间
			//if((ip->rwoff<<43)==0){
			delta =  (ip->rwoff-ip->rwsoff)>>g_block_size_shift;
			//userfs_info("ip->devid %d, ip->rwoff %lu, delta %d, ip->rwsoff %ld\n", ip->devid, ip->rwoff, (ip->rwoff-ip->rwsoff)>>g_block_size_shift, ip->rwsoff);
			if((delta+ (size>>g_block_size_shift)>512)||((ip->rwoff<<43)==0 && (ip->devid >0))||(ip->devid==0&&((ip->rwoff ==0)||(delta>=512)))){
				ip->wrthdnum = ip->wrthdnum % writethdnum;
			
				switch(ip->wrthdnum){
					/*
					case 0:
						ip->nvmoff = allocate_nvmblock(512);
						ip->ssdoff = ip->nvmoff;
						ip->devid=0;
						break;
					*/
					/*
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
					*/
					
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
			/*
			threadpool_add(writepool[ibh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)ibh);
#endif
			
			//老的索引此块的索引需不需要更新
			//upsert_blktree(ip, 1, addr, off, size);
		}
	}
	/*
	if(writespace>=size){
		bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
		bh->b_data=buf;
		bh->b_size=size;
		bh->b_offset=0;
			
		userfs_write(bh);
		addr= ip->ssdoff;
		ip->ssdoff +=size;
		append_fsextent(ip,off,bh,1);
		bh_release(bh);
		insert_blktree(ip,ip->devid+1, addr, off, size);
		//insert to blocktree
	}else{
		bh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
		bh->b_data=buf;
		bh->b_size=writespace;
		bh->b_offset=0;
			
		userfs_write(bh);
		addr = ip->ssdoff;
		ip->ssdoff +=writespace;
		append_fsextent(ip,off,bh,1);
		bh_release(bh);
		insert_blktree(ip,ip->devid+1, addr, off, writespace);
		//insert to blocktree

		ip->ssdoff=allocate_ssdblock(ip->devid);
		struct buffer_head *newbh;
		newbh = bh_get_sync_IO(ip->devid+1,ip->ssdoff>>12,BH_NO_DATA_ALLOC);
		newbh->b_data=buf;
		newbh->b_size=size-writespace;
		newbh->b_offset=0;
			
		userfs_write(newbh);
		addr = ip->ssdoff;
		ip->ssdoff +=size-writespace;
		append_fsextent(ip,off+writespace,newbh,1);
		bh_release(newbh);
		insert_blktree(ip,ip->devid+1, addr, off+writespace, size-writespace);
		//insert to blocktree
	}
	*/
	return size;
}

int append_unaligned_smallwrite(struct inode *ip, uint8_t *buf, offset_t off, uint32_t size){   //小于4K非对齐写
	//userfs_printf("unaligned_smallwrite off %d, write size %d, filesize %d\n",off,size,ip->size);
	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	struct buffer_head *bh;
	pthread_rwlock_rdlock(&ip->blktree_rwlock);
	struct block_node *blocknode = block_search(&(ip->blktree),off_aligned_4K);
	pthread_rwlock_unlock(&ip->blktree_rwlock);
	//userfs_info("blocknode %lu, off_aligned_4K %d\n",blocknode,off_aligned_4K);
	if(!blocknode){ //NULL
		bmap_req_t bmap_req;
		bmap_req.start_offset=off_aligned_4K;
		int ret=bmap(ip,&bmap_req);
		//printf("start_offset %d\n", off_aligned_4K);
		//userfs_info("bmap ret %d\n",ret);
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
			//ip->ssdoff += size;
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
			/*
			threadpool_add(writepool[bh->b_dev], userfs_write, (void *)warg);
			for(int i=1;i<=(nvmnum+ssdnum);i++){
				printf("writepool[i] queue_size %d\n",threadpool_queuesize(writepool[i]));
			}
			*/
			//threadpool_add(writethdpool, userfs_write, (void *)bh);
#endif
			//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
			//ip->ssdoff+=size;
			blocknode->size += size;
			//userfs_info("ip->ssdoff %ld\n",ip->ssdoff);
			//insert to blocktree
		}/*
		else if(off<ip->size){   //in-place 更新
			char *readbuf = malloc(g_block_size_bytes);
			struct buffer_head *readbh;
			unsigned long addr;
			
			//ip->nvmoff=allocate_nvmblock(1);  //分配一个nvm块做log写
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
				}
				ip->rwsoff = ip->rwoff;
				//userfs_info("allocate blocks ip->ino %d, ip->devid %d, ip->rwoff %ld, ip->wrthdnum %d, ip->rwsoff %lu\n",ip->inum,ip->devid, ip->rwoff,ip->wrthdnum,ip->rwsoff);
				__sync_fetch_and_add(&ip->wrthdnum,1);
			}

			bh = bh_get_sync_IO(1,ip->rwoff>>12,BH_NO_DATA_ALLOC);  

			readbh=bh_get_sync_IO(blocknode->devid,blocknode->addr+((off_aligned_4K-blocknode->fileoff)>>g_block_size_shift),BH_NO_DATA_ALLOC);
			//printf("bh_get_sync_IO\n");
			readbh->b_size=4096;
			readbh->b_offset =0;
			readbh->b_data = readbuf;
			//bh_submit_read_sync_IO(readbh);
			//printf("bh->b_data %s\n", bh->b_data);
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
			//threadpool_add(writethdpool, userfs_write, (void *)bh);
#endif
			

			
			
			
			//insert to blocktree
		}*/
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
	//printf("appendwrite ip->inum %d, filesize %d, off %d, size %d\n",ip->inum,ip->size,off,size);
	//printf("aligned write filesize %d, off %d, size %d\n",ip->size,off,size);
	uint64_t writesize=0;
	struct buffer_head *abh;
	unsigned long addr;
	unsigned long size_4K = (size+4095)>>g_block_size_shift;
	uint8_t devid;
	if(off == ip->size){ //append写，直接写入后面   如果前半部分写到nvm，如何安置后半部分
		//addr = allocate_nvmblock(size_4K);
		//devid = 0;
		
		//addr = allocate_ssdblocks(size_4K,0);
		//devid = 1;
		
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
	//struct write_arg *warg;
	//warg = (struct write_arg *)arg;
	//struct  inode *inode = warg->inode;
	//uint8_t *buf = warg->buf;
	//offset_t off = warg->off;
	//size_t size = warg->size;
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
	//printf("snfs_write filesize %d, off %d, size %d\n",ip->size,off, size);
	int retsize;


	offset_t off_aligned_4K = off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	//offset_t off_aligned_2M = off &(~(((uint64_t)0x1<<g_lblock_size_shift) -1));

	//offset_t offset_aligned= off &(~(((uint64_t)0x1<<g_block_size_shift) -1));
	uint32_t sized =size + off - off_aligned_4K;
	if(off_aligned_4K< off && sized >g_block_size_bytes){  //不对齐4K的情况
		//不对齐，且写的数据跨越一个块
		panic("write exist bug\n");
	}else if(off_aligned_4K< off){
		retsize = unaligned_smallwrite(ip,buf, off,size);
		//userfs_info("ip->ssdoff %d\n",ip->ssdoff);
		//userfs_info("retsize %d\n",retsize);
		return retsize;
	}

	if(off_aligned_4K == off){
		if(size < g_block_size_bytes){
			retsize=aligned_smallwrite(ip,buf, off,size);
			return retsize;
		}

		//here off_aligned_4K == off 4K对齐写
		//如果2M对齐，直接分配新的数据块

		retsize= aligned_write(ip,buf, off,size);
		//retsize= appendwrite(ip,buf, off,size);
		return retsize;
	}
/*	
	if(off_aligned_4K==off_aligned_2M){


		retsize= aligned_2M_write(ip,buf, off,size);
		//userfs_info("retsize %d\n",retsize);
*/
/*		if((off+size) <= (FILENVMBLOCK<<g_block_size_shift) || ((ip->nvmblock ==0) &&(FILENVMBLOCK >=(size>>g_block_size_shift)))){
		retsize=fs_nvmwrite(ip, buf, off, size);
		if(retsize==-1)
			retsize = fs_ssdwrite(ip, buf, off, size);
		}else{

			retsize=fs_ssdwrite(ip, buf, off, size);
		}
		//if (size > 0 && (off + size) > ip->size) 
		//	ip->size = off + size;
		return retsize;
		*/
/*	}else{ //否则，根据前一个数据块的位置确定写位置
		retsize= aligned_4K_write(ip,buf, off,size);
		//userfs_info("retsize %d\n",retsize);
	}
	//userfs_info("ip->ssdoff %d\n",ip->ssdoff);
	return retsize;
	*/
	//if(off < ip->nvmoff && fd<){
	//if(off < ip->nvmoff){

	//printf("offset %d, ip->nvmblock %d\n", off,ip->nvmblock);
	//printf("size %d, %d\n", size, FILENVMBLOCK>>g_block_size_shift);
/*
	if((off+size) <= (FILENVMBLOCK<<g_block_size_shift) || ((ip->nvmblock ==0) &&(FILENVMBLOCK >=(size>>g_block_size_shift)))){
		retsize=fs_nvmwrite(ip, buf, off, size);
		if(retsize==-1)
			retsize = fs_ssdwrite(ip, buf, off, size);
	}else{

		retsize=fs_ssdwrite(ip, buf, off, size);
	}
	//if (size > 0 && (off + size) > ip->size) 
	//	ip->size = off + size;
	return retsize;
	*/
}

// Write to file f.
int userfs_file_write(struct file *f, uint8_t *buf, size_t n)
{
	//printf("userfs_file_write offset %d, size %d\n",f->off,n);
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
	/*
	// PIPE is not supported 
	if(f->type == FD_PIPE)
		return pipewrite(f->pipe, buf, n);
	*/
	struct  inode * inode =f->ip;
	file_size = inode->size;

	//printf("userfs_file_write offset %d, count %d, file_size %d\n",f->off,n,file_size);
	//printf("f->off %d,size %d\n", f->off,n);
	//userfs_printf("filesize %d\n", inode->size);
	if (n > 0 && (f->off + n) > inode->size) 
		file_size = f->off + n;

	//userfs_printf("filesize %d\n", inode->size);
	//printf("filesize %d\n", file_size);
	//inode->size和 f->off , size 
	//判断多少数据是inplace update
	//多少数据是append write

	//userfs_info("filesize %d,new filesize %d\n", inode->size,file_size);
/*	printf("inode->nvmspace %lu\n", inode->nvmspace);
	printf("inode->ssdspace %lu\n", inode->ssdspace);
	printf("inode->nvmsize %d\n", inode->nvmsize);
	printf("inode->ssdsize %d\n", inode->ssdsize);
	if(!inode->nvmsize && !inode->ssdsize){
		uint32_t nvmsize = 1024, ssdsize = 65536;
		allocate_block(inode, nvmsize, ssdsize);
		printf("allocate success\n");
	}
	*/
	//printf("f->type %d\n",f->type);
	//f->type=FD_INODE;
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
		//struct fuckfs_extent * fextent;
		//fextent = (struct fuckfs_extent *)malloc(sizeof(fuckfs_extent));
		//struct userfs_extent *extent;
		//struct timeval curtime;

		userfs_debug("%s\n", "+++ start transaction");

		//start_log_tx();

		offset_start = f->off;
		offset_end = f->off + n;

		offset_aligned = ALIGN(offset_start, g_block_size_bytes);
		//printf("offset_start is %d\n",offset_start);
		//printf("offset_end is %d\n",offset_end);
		//printf("offset_aligned is %d\n",offset_aligned);
		/* when IO size is less than 4KB. */
		if (offset_end < offset_aligned) { 
			//printf("if \n");
			size_prepended = n;
			size_aligned = 0;
			size_appended = 0;
		} else {
			//printf("else \n");
			// compute portion of prepended unaligned write
			if (offset_start < offset_aligned) {
				size_prepended = offset_aligned - offset_start;
			} else
				size_prepended = 0;

			userfs_assert(size_prepended < g_block_size_bytes);

			//printf("size_prepended is %d\n",size_prepended);
			// compute portion of appended unaligned write
			size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
			if (size_appended > 0)
				size_appended = g_block_size_bytes - size_appended;

			size_aligned = n - size_prepended - size_appended; 
			//printf("size_appended is %d\n",size_appended);
			//printf("size_aligned is %d\n",size_aligned);
		}
		userfs_printf("inode->inum %d, filesize %d, size_prepended %d, size_appended %d, size_aligned %d\n",inode->inum,inode->size,size_prepended,size_appended,size_aligned);
		//userfs_printf("filesize %d\n", inode->size);
		if (size_prepended > 0) {
			//userfs_info("if (size_prepended > 0)  %d\n",size_prepended);
			ilock(inode);

			//userfs_printf("filesize %d\n", inode->size);
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


			//printf("io_size %d, max_io_size %d, f->off %d\n",io_size, max_io_size,f->off);
			if(io_size > max_io_size)
				io_size = max_io_size;

			fileoff = f->off + io_size;
			aligned_2M_upoff = (f->off + ((uint64_t)0x1<<g_lblock_size_shift)) & ~ (((uint64_t)0x1<<g_lblock_size_shift)-1);
			//userfs_info("fileoff %ld, aligned_2M_upoff %ld\n",fileoff,aligned_2M_upoff);
			if(fileoff>= aligned_2M_upoff){
				io_size = aligned_2M_upoff - f->off;
			}
			/* add_to_log updates inode block pointers */
			ilock(inode);
			//userfs_printf("filesize %d\n", inode->size);
			if ((r = snfs_write(f->fd,inode,buf+i,f->off,io_size)) > 0){
				if (r > 0 && (f->off + r) > inode->size) 
					inode->size = f->off + r;
				f->off += r;
			}

			iunlock(inode);
			/* do not copy user buffer to page cache */
			
			/* add buffer to log header */
			//if ((r = add_to_log(f->ip, buf + i, f->off, io_size)) > 0)
			//	f->off += r;

			/* jugde the write size 

			extent= write_data(f, buf, n)；
			fextent->ino = f->ip->inum;
			fextent->entry_type = FILE_WRITE;
			fextent->device_id = dev;
			gettimeofday(&curtime,NULL);
			fextent-> mtime =curtime.tv_sec;
			fextent->extent.ee_block = extent->ee_block;
			fextent->extent.ee_len = extent->ee_len;  
			fextent->extent.ee_start_hi = extent->ee_start_hi;
			fextent->extent.ee_start_lo = extent->ee_start_lo;
			append_entry_log(fextent);
			*/
			/*
			int tret;
			warg[tn] = (struct write_arg *)malloc(sizeof(struct write_arg));
			warg[tn]->inode = f->ip;
			warg[tn]->buf = buf +i;
			warg[tn]->off=f->off;
			warg[tn]->size= io_size;
			*/
			//userfs_info("create thread %d\n", tn);
/*			if(isnvm<10){
				char *writeaddr=f->ip->writespace;
				fs_writen(writeaddr,offset,buf,io_size);
				//memcpy(writeaddr+f->off,buf,io_size);
				//printf("write nvm addr %ld\n",f->ip->writespace);
				//memcpy(mmapstart+nvmstartaddr,buf,io_size);
				//warg[tn]->addr=nvmstartaddr;
				//fuckfs_pwriten(warg[wn++]);
				//int tret = pthread_create(&tid[tn], NULL, fuckfs_pwriten, (void *)warg[tn++]);
				//if(tret){  
            	//	userfs_info("Can't create thread %d\n", tn-1);
        		//}
        		//printf("write nvm sucesss\n");
        		//nvmstartaddr+=io_size;
        	}else{
        		printf("write ssd\n");
        		warg[tn]->addr=ssdstartaddr;
				int tret = pthread_create(&tid[tn], NULL, fuckfs_pwrites, (void *)warg[tn++]);
				if(tret){  
            		userfs_info("Can't create thread %d\n", tn-1);
        		}
        		ssdstartaddr+=io_size;
        	}
        	isnvm++;
        	isnvm=isnvm%10;
        	*/
/*			userfs_info("create thread %d\n", tn);
			int tret = pthread_create(&tid[tn], NULL, fuckfs_pwrite, (void *)warg[tn++]);
			if(tret){  
            	userfs_info("Can't create thread %d\n", tn-1);
        	}  
*/
			//r = fuckfs_write(f->ip, buf + i, f->off, io_size);
			//将write的块地址插入到rbtree中，------------------------------------------
			//printf("1\n");
			//f->off += io_size;
			//iunlock(f->ip);

			//printf("2\n");

			if(r < 0)
				break;


			//userfs_info("return r is %d, io_size is %d\n",r,io_size);
			if(r != io_size)
				panic("short filewrite");

			i += r;


			//printf("i %d\n", i);
		
		}

		// add appended portion to log
		if (size_appended > 0) {

			//userfs_info("if (size_prepended > 0) %d\n",size_appended);
			ilock(inode);
			//userfs_printf("filesize %d\n", inode->size);
			r = snfs_write(f->fd,inode,buf+i,f->off,size_appended);
			//extent= write_data(f, buf, size_t n)；
			//r = add_to_log(f->ip, buf + i, f->off, size_appended);
			/*struct fuckfs_extent {
			__le64	ino;
			u8	entry_type;
			u8 device_id;
			__le32	mtime;
			struct userfs_extent extent; //96
			};
			*/
		/*	
			warg[tn] = (struct write_arg *)malloc(sizeof(struct write_arg));
			warg[tn]->inode = f->ip;
			warg[tn]->buf = buf +i;
			warg[tn]->off = f->off;
			warg[tn]->size= size_appended;
			
			int tret = pthread_create(&tid[tn], NULL, fuckfs_pwrite, (void *)warg[tn++]);
			if(tret){  
            	userfs_info("Can't create thread %d\n", tn-1);
        	}
*/
			//r = fuckfs_write(f->ip, buf + i, f->off, size_appended);
			//append_entry_log(fextent);
			if (r > 0 && (f->off + r) > inode->size) 
				inode->size = f->off + r;

			iunlock(inode);

			userfs_assert(r > 0);

			f->off += r;

			i += r;


		}
/*		size_t remain;
		int res=pthread_join(tid[tn-1], NULL);
		if(!res){
			printf("thread %d joined\n", tn-1);
		}else{
			printf("thread %d joined failed\n",tn-1);
		}
		*/
//		printf("tid %d, remain %d\n", tn-1,remain);
//		if(remain!=0){
//			printf("%d thread write error\n", tn-1);
//		}
//		printf("pthread_join\n");
//		
		//int res  = read_join(tid[0], (void*)&remain);
		//if(res != 0)
  		//{
    	//	printf("thread join error");
    	//}
		//printf("tid 0, remain %d\n",remain);
/*		int res;
		for(int j=0;j < 1; j++){
			printf("pthread_join %d\n",j);
			//res=pthread_join(tid[0], (void*)&remain);
			res=pthread_join(tid[], NULL);
			if(!res){
				printf("thread %d joined, remain %d\n", j,remain);
			}else{
				printf("thread %d joined failed\n",j);
			}
			if(remain!=0){
				printf("%d thread write error\n", j);
			}
		}
*/
		/* Optimization: writing inode to log does not require
		 * for write append or update. Kernfs can see the length in inode
		 * by looking up offset in a logheader and also mtime in a logheader */
		
		//iupdate(f->ip); //需要更新inode
		
		//commit_log_tx();

		//pthread_join(tid[tn], (void*)&rv);

	
		userfs_debug("%s\n", "--- end transaction");
		//userfs_info("filesize %d,inode->size %d\n", file_size,inode->size);
		//printf("filesize %d,inode->size %d\n", file_size,inode->size);
		inode->size = file_size;
		//userfs_printf("filesize %d,inode->size %d\n", file_size,inode->size);
		//userfs_info("userfs_file_write ret %d\n",n);
		//return n;
		return i == n ? n : -1;
	}

	panic("filewrite");

	return -1;
}

//bug
struct inode *fetch_ondisk_inode_name(char *path)
{
	//userfs_info("fetch_ondisk_inode_name %s\n",path);
	struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
	struct inode * parent_inode = NULL;
	int ret;
	ret = syscall(328, path, tinode, 0, 1, 0);
	//printf("ret is %d\n", ret);
	if(ret>0){
		//parent_inode = (struct inode *) malloc(sizeof(struct inode));
		parent_inode = inodecopy(tinode);
		art_tree_init(&parent_inode->dentry_tree);
		parent_inode->inmem_den =0;
		//如何获取数据
		//printf("parent_inode->root is %d\n", parent_inode->root);
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
    //inode->i_opflags = tinode->i_opflags;
    //inode->i_uid = tinode->i_uid;
    //inode->i_gid = tinode->i_gid;
    //inode->flags = tinode->i_flags;
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

//char testname[256]="/mnt/pmem_emul/bigfileset/00000001/00000002";

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

//	if(isroot(path)){
		//return root_inode;
//	}
	//printf("call get_parent_path\n");
	//struct inode *tinode = dlookup_find(testname);
	//printf("tinode %lu\n", tinode);
	inode=dlookup_find(path);
	if(inode!=NULL)
	{
		userfs_info("dir_lookup inode path %s\n",path);
		inode->i_ref++;
		//userfs_info("find inode in dlookup_find inode->nvmsize %d\n",inode->nvmsize);
		//userfs_info("inode->usednvms %d\n",inode->usednvms);
		return inode;
	}

	
	get_parent_path(path,parent_path,filename);
	//printf("end get_parent_path\n");
	userfs_info("path is %s, parent_path is %s, and filename is %s\n",path,parent_path,filename);
	if ((parent_inode = nameiparent(path, name)) == 0){  //在hash table中查找inode
		userfs_printf("parent directory %s does not exist!!!!\n",parent_path);
		return NULL;
		//printf("path is %s, parent_path is %s, and filename is %s\n",path,parent_path,filename);
		//printf("name is %s\n",name);

		parent_inode=fetch_ondisk_inode_name(parent_path);//bug
		
		dlookup_alloc_add(parent_inode,parent_path);    //HASH_ADD_STR 插入inode
	}
	//printf("name is %s\n",name);
	//printf("parent_path is %s\n",parent_path);
	//printf("parent_inode root is %d\n",parent_inode->root);
	//printf("parent_inode inum is %d\n",parent_inode->inum);
	//printf("parent_inode %lu\n", parent_inode);

	//userfs_printf("parent_inode i_ref %d\n",parent_inode->i_ref);
	inode=de_cache_find(parent_inode,name);
	//userfs_info("de_cache_find %lu\n",inode);
	if(inode!=NULL){
		inode->i_ref++;
		//userfs_printf("userfs_open_file success path %s, ref %d\n",path,inode->i_ref);
		return inode;
	}

	ilock(parent_inode);
	//printf("ilock\n");
	if (enable_perf_stats) 
		tsc_begin = asm_rdtscp();
	//printf("userfs_object_create\n");
	
	//unsigned long hash;
	//int namelen = sizeof(path);
	//hash = BKDRHash(path, namelen);
	//printf("hash %lu\n",hash);
	//printf("userfs_object_create\n");
	//get_parent_path(parent_path,ppname,name);
	//printf("parent_path is %s, ppname is %s, and name is %s\n",parent_path,ppname,name);
	//printf("dir_inode %d art_search %s\n",parent_inode->inum, filename);

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
	//printf("1ude is %lu\n", uden);


	//获取父目录的on-disk dentries
	//userfs_info("parent_inode ino %d, name %s, root %lu\n",parent_inode->inum, name,parent_inode->root);
	//if(uden == NULL && parent_inode->inmem_den==0 &&parent_inode->root>0){

	if(uden == NULL && parent_inode->root>0 && parent_inode->inmem_den==0){
		//printf("fetch_ondisk_dentry_from_kernel\n");
		uden=fetch_ondisk_dentry_from_kernel(parent_inode,name);
		fetch_ondisk_dentry(parent_inode);
		parent_inode->inmem_den=1;
		//dir_inode->inmem_den=1;
		/*
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
		uden=art_search(&dir_inode->dentry_tree, name, strlen(name));
		*/
		//printf("2uden %lu\n", uden);
	}
	/*
	if(uden == NULL&&parent_inode->root>0){
		//printf("fetch_ondisk_dentry_from_kernel\n");
		uden=fetch_ondisk_dentry_from_kernel(parent_inode,name);
		//parent_inode->inmem_den=1;

		//printf("fetch_ondisk_dentry\n");
		//fetch_ondisk_dentry(parent_inode);
		//parent_inode->inmem_den=1;
		//uden=art_search(&parent_inode->dentry_tree, filename, strlen(filename));
		//printf("2uden %lu\n", uden);
	}
	*/
	//if(uden!=NULL){
	//	printf("2uden->name is %s\n",uden->direntry->name);
	//}
	//direntry=uden->direntry;
	//userfs_info("uden %lu\n", uden);
	//userfs_info("uden->addordel %d\n", uden->addordel);
	if(uden == NULL || uden->addordel==Delden){ //不存在此文件dentry，即不存在此文件或文件被删除
		userfs_printf("not exist file %s or dir \n",path);
		if(uden==NULL){
			uden= (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
		}
		//printf("malloc\n");
		//uden= (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
		struct fs_direntry * direntry= (struct fs_direntry *)malloc(sizeof(struct fs_direntry));
		//printf("uden %lu, direntry %lu\n", uden,direntry);
		// create new inode
		//根据inode_bitmap分配一个inode好，

		inum =find_inode_bitmap();
		//userfs_info("find_inode inum %d\n", inum);
		if(inum==0){
			panic("cannot allocate inode\n");
			return NULL;
		}
		//printf("creat inode\n");
		// create new inode
		//根据inode_bitmap分配一个inode好，
		//printf("inum is %d\n", inum);
		//(struct inode *)malloc(sizeof(struct inode));
		//inum=inum>>inode_shift;
		struct timeval curtime;
		//inode = (struct inode *) malloc(sizeof(struct inode));
		//printf("icache_alloc_add0\n");
		inode=icache_alloc_add(inum);
		//printf("icache_alloc_add1\n");
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
		//inode->fileno =0;
		//inode->writespace = NULL;
		inode->nvmblock=0;
		inode->ssdoff=0;
		inode->nvmoff=0;
		inode->rwoff = 0;
		inode->rwsoff=0;
		inode->root=0;
		inode->fd=-1;
		//pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);
		//printf("ialloc3\n");
		//pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
		//ip->fcache = NULL;
		//ip->n_fcache_entries = 0;
		pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

		pthread_rwlock_init(&inode->fcache_rwlock, &rwlattr);
		//ip->fcache = NULL;
		//ip->n_fcache_entries = 0;

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
		//inode->nvmspace = (struct space *)malloc(sizeof(struct space));
		//inode->ssdspace = (struct space *)malloc(sizeof(struct space));
		//inode->nvmsize=0;
		inode->ssdsize=0;
		//printf("inode INIT_LIST_HEAD\n");
		//INIT_LIST_HEAD(&(inode->nvmspace->list));
		//INIT_LIST_HEAD(&(inode->ssdspace->list));

		//inode->allonvmspace = (struct space *)malloc(sizeof(struct space));
		//inode->allossdspace = (struct space *)malloc(sizeof(struct space));
		//inode->allonvmsize=0;
		//inode->allossdsize=0;
		//INIT_LIST_HEAD(&(inode->allonvmspace->list));
		//INIT_LIST_HEAD(&(inode->allossdspace->list));

		inode->blktree=RB_ROOT;
		pthread_rwlock_init(&inode->blktree_rwlock, &rwlattr);

		inode->de_cache = NULL;
		pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&inode->i_mutex, NULL);

		//add_to_loghdr(L_TYPE_INODE_CREATE, inode, 0, 
		//		sizeof(struct dinode), NULL, 0);
		userfs_printf("create %s - inum %u\n", path, inode->inum);
		userfs_debug("create %s - inum %u\n", path, inode->inum);

		if (type == T_DIR) {
			// Add "." and ".." to direntry
			// this link up is for ".."
			// To avoid cyclic ref count, do not bump link for "."
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
		//iupdate(parent_inode);   //need to update
		
		direntry->ino = inum;
		direntry->file_type=type;
		strncpy(direntry->name,filename,DIRSIZ);
		//uden->logoffset=logoffset;
		//uden->generation=0;
		uden->addordel=Addden;
		uden->direntry=direntry;

		//userfs_printf("parent_path %s, parent_inode->dentry_tree %lu, art_insert %s\n",parent_path,&parent_inode->dentry_tree, filename);
		//printf("filename is %s, namelen is %d\n", filename,strlen(filename));
		pthread_rwlock_wrlock(&parent_inode->art_rwlock);
		ret = art_insert(&parent_inode->dentry_tree, filename, strlen(filename), uden);
		pthread_rwlock_unlock(&parent_inode->art_rwlock);
		/*
		if(!ret){
			panic("art insert error\n");
			return NULL;
		}
		*/
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
		//attrentry->mtime=curtime.tv_sec;
		attrentry->ctime=curtime.tv_sec;
		attrentry->size=0;
		append_entry_log(attrentry,0);
		//printf("path is %s, parent_path is %s, and filename is %s\n",path,parent_path,filename);

		//printf("fdentry->name is %s\n",filename);
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
		//fdentry->size=;
		strncpy(fdentry->name,filename,DIRSIZ);	/* File name */
		//printf("fdentry->ino %d\n", fdentry->ino);
		//printf("fdentry->fino %d\n", fdentry->fino);
		append_entry_log(fdentry, 1);

		
	}else{
		//userfs_info("fetch_ondisk_inode_ino %d\n",uden->direntry->ino);
		//if(uden->addordel==Delden){
		//	printf("%s not exist!\n", filename);
		//	return NULL;
		//}
	
		//inode = fetch_ondisk_inode_ino(uden->direntry->ino);
		inode=read_ondisk_inode_from_kernel(uden->direntry->ino);
		//userfs_info("inode %lu\n",inode);
		//userfs_info("inode size %d, height %d, root %d\n",inode->size,inode->height,inode->root);
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
		//inode->nvmspace = (struct space *)malloc(sizeof(struct space));
		//inode->ssdspace = (struct space *)malloc(sizeof(struct space));
		//printf("inode INIT_LIST_HEAD\n");
		//INIT_LIST_HEAD(&(inode->nvmspace->list));
		//INIT_LIST_HEAD(&(inode->ssdspace->list));

		//inode->allonvmspace = (struct space *)malloc(sizeof(struct space));
		//inode->allossdspace = (struct space *)malloc(sizeof(struct space));
		//inode->allonvmsize=0;
		//inode->allossdsize=0;
		//INIT_LIST_HEAD(&(inode->allonvmspace->list));
		//INIT_LIST_HEAD(&(inode->allossdspace->list));

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

	//userfs_info("inode->fcache_hash %lu\n", inode->fcache_hash);
	//userfs_info("inode->i_ref %d\n",inode->i_ref);
	//INIT_LIST_HEAD(&inode->i_slru_head);
	
	//pthread_mutex_init(&inode->i_mutex, NULL);

/*
	if(type == T_DIR){
		art_tree_init(&inode->dentry_tree);
	}
*/
	
	

	//printf("2create %s - inum %u\n", path, inode->inum);
	
	//lru_upsert(parent_inode->inum,parent_path);
	//lru_upsert(inum,path);

	//將此文件entry加入到父目錄的数据中，即加入radix tree.


	//if (dir_add_entry(parent_inode, name, inode->inum) < 0)  //need to update
	//	panic("cannot add entry");
	//printf("create %s - inum %u\n", path, inode->inum);
	iunlockput(parent_inode);
	//printf("3create %s - inum %u\n", path, inode->inum);
	//userfs_info("name %s,path %s\n", name, path);
	

	


	de_cache_alloc_add(parent_inode, name, inode);

	
	if (!dlookup_find(path))
		dlookup_alloc_add(inode, path);  //HASH_ADD_STR(inode)
	//dlookup_find(path);
	//userfs_info("end userfs_open_file %lx\n",inode);
	userfs_printf("userfs_open_file sucesss path %s, ref %d\n",path,inode->i_ref);
	return inode;
}
/*
struct inode *userfs_open_files(char *path, unsigned short type)
{
	offset_t offset;
	struct inode *inode, *parent_inode = NULL;
	char name[DIRSIZ], parent_path[DIRSIZ], filename[DIRSIZ],ppname[DIRSIZ];
	uint64_t tsc_begin, tsc_end;
	int ret;
	if(isroot(path)){
		//return root_inode;
	}
	if ((parent_inode = nameiparent(path, name)) == 0){  //在hash table中查找inode

		get_parent_path(path,parent_path,filename);
		//printf("path is %s, parent_path is %s, and filename is %s\n",path,parent_path,filename);
		parent_inode=fetch_ondisk_inode_name(parent_path);
		//printf("parent_inode is %d\n",parent_inode->inum);
		dlookup_alloc_add(parent_inode,parent_path);    //HASH_ADD_STR 插入inode
	}
	ilock(parent_inode);
	//printf("userfs_object_create\n");
	if (enable_perf_stats) 
		tsc_begin = asm_rdtscp();
	//printf("userfs_object_create\n");
	struct fs_direntry * direntry;
	//unsigned long hash;
	//int namelen = sizeof(path);
	//hash = BKDRHash(path, namelen);
	//printf("hash %lu\n",hash);
	printf("userfs_object_create\n");
	get_parent_path(parent_path,ppname,name);
	printf("parent_path is %s, ppname is %s, and name is %s\n",parent_path,ppname,name);
	direntry = art_search(&parent_inode->dentry_tree, name, strlen(name));
	if(direntry == NULL){ //不存在此此文件dentry，即不存在此文件
		printf("direntry == NULL\n");
		direntry= (struct fs_direntry *)malloc(sizeof(struct fs_direntry));
		// create new inode
		//根据inode_bitmap分配一个inode好，
		uint32_t inum =find_inode_bitmap();
		if(inum<=0){
			panic("cannot allocate inode\n");
			return NULL;
		}
		// create new inode
		//根据inode_bitmap分配一个inode好，
		printf("inum is %d\n", inum);
		//(struct inode *)malloc(sizeof(struct inode));
		inode = (struct inode *) malloc(sizeof(struct inode));
		inode->inum = inum;
		inode->flags = 0;
		inode->flags |= I_VALID;
		inode->i_ref = 1;
		inode->n_de_cache_entry = 0;
		inode->i_dirty_dblock = RB_ROOT;
		inode->i_sb = sb;
		inode->fcache = NULL;
		inode->n_fcache_entries = 0;

		if (!inode)
			panic("cannot create inode");
		
		if (enable_perf_stats) {
			tsc_end = asm_rdtscp();
			g_perf_stats.ialloc_tsc += (tsc_end - tsc_begin);
			g_perf_stats.ialloc_nr++;
		}

		inode->itype = type;
		inode->nlink = 1;

		//add_to_loghdr(L_TYPE_INODE_CREATE, inode, 0, 
		//		sizeof(struct dinode), NULL, 0);
		printf("create %s - inum %u\n", path, inode->inum);
		userfs_debug("create %s - inum %u\n", path, inode->inum);

		if (type == T_DIR) {
			// Add "." and ".." to direntry
			// this link up is for ".."
			// To avoid cyclic ref count, do not bump link for "."
			parent_inode->nlink++;

	#if 0
			if (dir_add_entry(inode, ".", inode->inum) < 0)
				panic("cannot add . entry");

			if (dir_add_entry(inode, "..", parent_inode->inum) < 0)
				panic("cannot add .. entry");
	#endif

			iupdate(parent_inode);   //need to update
			direntry->ino = inum;
			strncpy(direntry->name,filename,DIRSIZ);
			printf("filename is %s, namelen is %d\n", filename,strlen(filename));
			ret = art_insert(&parent_inode->dentry_tree, filename, strlen(filename), direntry);
			/*
			if(!ret){
				panic("art insert error\n");
				return -1;
			}
			*/
/*
			if (ret)
				userfs_debug("%s ERROR %d: %s\n", __func__, ret, path);
		}
	}else{
		inode = fetch_ondisk_inode_ino(direntry->ino);
		inode->itype =type;
	}
	if(type == T_DIR){
		art_tree_init(&inode->dentry_tree);
	}
	
	
	

	printf("create %s - inum %u\n", path, inode->inum);
	
	
	

	//將此文件entry加入到父目錄的数据中，即加入radix tree.
	

	//if (dir_add_entry(parent_inode, name, inode->inum) < 0)  //need to update
	//	panic("cannot add entry");
	//printf("create %s - inum %u\n", path, inode->inum);
	iunlockput(parent_inode);
	printf("create %s - inum %u\n", path, inode->inum);


	if (!dlookup_find(path))
		dlookup_alloc_add(inode, path);  //HASH_ADD_STR(inode)
	printf("end userfs_open_file %lx\n",inode);
	return inode;
}



*/
