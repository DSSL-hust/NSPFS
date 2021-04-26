#include "filesystem/fs.h"
#include "io/block_io.h"

int namecmp(const char *s, const char *t)
{
	return strncmp(s, t, DIRSIZ);
}

static inline int dcache_del(struct dirent_block *dir_block)
{
	pthread_rwlock_rdlock(dcache_rwlock);

	HASH_DELETE(hash_handle, dirent_hash, dir_block);

	pthread_rwlock_unlock(dcache_rwlock);

	return 0;
}

static inline struct dirent_block *dcache_find(	uint32_t inum, offset_t _offset)
{
	struct dirent_block *dir_block;
	dcache_key_t key = {
		.inum = inum,
		.offset = (_offset >> g_block_size_shift),
	};

	pthread_rwlock_rdlock(dcache_rwlock);

	HASH_FIND(hash_handle, dirent_hash, &key,
				sizeof(dcache_key_t), dir_block);

	pthread_rwlock_unlock(dcache_rwlock);

	return dir_block;
}

static inline struct dirent_block *dcache_alloc_add(uint8_t dev, uint32_t inum, 
		offset_t offset, uint8_t *data, addr_t log_addr)
{
	struct dirent_block *dir_block;

	dir_block = (struct dirent_block *)userfs_zalloc(sizeof(*dir_block));
	if (!dir_block)
		panic("Fail to allocate dirent block\n");

	dir_block->key.inum = inum;
	dir_block->key.offset = (offset >> g_block_size_shift);

	if (data)
		memmove(dir_block->dirent_array, data, g_block_size_bytes);
	else
		memset(dir_block->dirent_array, 0, g_block_size_bytes);

	dir_block->log_addr = log_addr;

	userfs_debug("add (DIR): inum %u offset %lu -> addr %lu\n", 
			inum, offset, log_addr);

	pthread_rwlock_wrlock(dcache_rwlock);

	HASH_ADD(hash_handle, dirent_hash, key,
			sizeof(dcache_key_t), dir_block);

	pthread_rwlock_unlock(dcache_rwlock);

	return dir_block;
}


uint8_t *get_dirent_block(struct inode *dir_inode, offset_t offset)
{
	int ret;
	struct buffer_head *bh;
	struct dirent_block *d_block;
	addr_t block_no;
	// Currently, assuming directory structures are stored in NVM.
	uint8_t dev = g_root_dev;

	userfs_assert(dir_inode->itype == T_DIR);

	d_block = dcache_find(dir_inode->inum, offset);

	if (d_block) 
		return d_block->dirent_array;

	if (dir_inode->size == 0) {
		// directory is empty
		if (!(dir_inode->dinode_flags & DI_VALID))
			panic("dir_inode is not synchronized with on-disk inode\n");

		d_block = dcache_alloc_add(dev, dir_inode->inum, 0, NULL, 0);
	} else {
		bmap_req_t bmap_req = {
			.dev = dev,
			.start_offset = offset,
			.blk_count = 1,
		};

		// get block address
		ret = bmap(dir_inode, &bmap_req);
		userfs_assert(ret == 0);
		// requested directory block is not allocated in kernfs.
		if (bmap_req.blk_count_found == 0) {
			d_block = dcache_alloc_add(dev, dir_inode->inum, offset, NULL, 0);
			userfs_assert(d_block);
		} else {
			uint8_t *data = userfs_alloc(g_block_size_bytes);
			bh = bh_get_sync_IO(1, bmap_req.block_no, BH_NO_DATA_ALLOC);

			bh->b_size = g_block_size_bytes;
			bh->b_data = data;

			bh_submit_read_sync_IO(bh);

			userfs_io_wait(1, 1);

			d_block = dcache_alloc_add(dev, dir_inode->inum, 
					offset, data, bmap_req.block_no);

			userfs_assert(d_block);
		}
	}

	return d_block->dirent_array;
}

struct fs_direntry *get_dirent(struct inode *dir_inode, offset_t offset)
{
	struct fs_direntry *dir_entry;
	uint8_t *dirent_array;

	dirent_array = get_dirent_block(dir_inode, offset);

	dir_entry = (struct fs_direntry *)(dirent_array + (offset % g_block_size_bytes));

	return dir_entry;
}


int dir_check_entry_fast(struct inode *dir_inode) 
{
	// Avoid brute force search of directory blocks.
	// de_cache has caching of all files in the directory.
	// So cache missing means there is no file in the directory.
	if ((dir_inode->n_de_cache_entry == 
				bitmap_weight(dir_inode->dirent_bitmap, DIRBITMAP_SIZE)) &&
			dir_inode->n_de_cache_entry > 2) {
		userfs_debug("search skipped %d %d\n", dir_inode->n_de_cache_entry,
				bitmap_weight(dir_inode->dirent_bitmap, DIRBITMAP_SIZE));
		return 0;
	}
	
	return 1;
}

// Search for an inode by name from directory entries.
// dir_inode is an inode for the directory entries.
// If found, set *poff to byte offset of entry.
// dir_lookup increase i_ref by iget. caller must call iput in proper code path.
struct inode* dir_lookup(struct inode *dir_inode, char *name, struct ufs_direntry *uden)
{
	//offset_t off = 0;
	uint32_t inum, n;
	//struct userfs_dirent *de;
	struct fs_direntry *pde;
	struct inode *ip;
	uint64_t tsc_begin, tsc_end;
	char *blk_base,*dlimit;
	unsigned short de_len;

	if (dir_inode->itype != T_DIR)
		panic("lookup for non DIR");

	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();


	ip = de_cache_find(dir_inode, name);
	if (ip) {
		ip->i_ref++;
		if (enable_perf_stats) {
			g_perf_stats.dir_search_nr_hit++;
		}
		return ip;
	}
	
	pthread_rwlock_rdlock(&dir_inode->art_rwlock);
	uden=art_search(&dir_inode->dentry_tree, name,strlen(name));
	pthread_rwlock_unlock(&dir_inode->art_rwlock);
	if(uden == NULL && dir_inode->root>0 && dir_inode->inmem_den==0){
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
	}
	pthread_rwlock_rdlock(&dir_inode->art_rwlock);
	uden=art_search(&dir_inode->dentry_tree, name,strlen(name));
	pthread_rwlock_unlock(&dir_inode->art_rwlock);


	if(uden == NULL && dir_inode->root>0){
		uden=fetch_ondisk_dentry_from_kernel(dir_inode,name);
	}
	if(uden!=NULL){
		if(uden->addordel==Delden){
			userfs_info("%s not exist\n", name);
			return NULL;
		}
		pde=uden->direntry;
		inum = pde->ino;
		if(inum<=0){
			userfs_printf("inode num %d wrong\n", inum);
			return NULL;
		}
		ip = iget(inum);
		ip->itype = pde->file_type;
		userfs_assert(ip);
		de_cache_alloc_add(dir_inode, name, ip);
	
		return ip;

	}

	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		g_perf_stats.dir_search_nr_notfound++;
	}

	return NULL;
}

/* linux_dirent must be identical to gblic kernel_dirent
 * defined in sysdeps/unix/sysv/linux/getdents.c */
int dir_get_entry(struct inode *dir_inode, struct linux_dirent *buf, offset_t off)
{
	struct inode *ip;
	uint8_t *dirent_array;
	//struct userfs_dirent *de;
	struct fs_direntry *den;

	den = get_dirent(dir_inode, off);

	userfs_assert(den);

	buf->d_ino = den->ino;
	buf->d_off = (off/sizeof(*den)) * sizeof(struct linux_dirent);
	buf->d_reclen =sizeof(struct linux_dirent);
	memmove(buf->d_name, den->name, DIRSIZ);

	return sizeof(struct fs_direntry);
}
	
/* Workflows when renaming to existing one (newname exists in the directory).
 * Libfs: if it finds existing file, it makes unlink request to previous inode.
 *        UNLINK of previous newname, but does not make unlink log entry.
 *        DIR_DEL of oldname
 *        DIR_RENAME of newname (inode number is the same as oldname).
 *
 * Kernfs: when it gets,
 *        DIR_DEL of oldname : delete oldname in the directory
 *        DIR_RENAME of newname: it deletes an existing newname and add newname
 *        to directory (inode number is different). 
 *        If there exist a newname, kernfs unlink it while digesting DIR_RENAME.
 */
int dir_change_entry(struct inode *dir_inode, char *oldname, char *newname)
{
	//userfs_info("dir_change_entry %s->%s\n",oldname,newname);
	struct inode *ip;
	uint8_t *dirent_array;
	//struct userfs_dirent *de;
	//offset_t off = 0;
	int ret = -ENOENT;
	uint32_t n;
	uint64_t tsc_begin, tsc_end;
	char token[DIRSIZ+8];
	char *dlimit;
	char *blk_base;
	int de_len;
	struct fs_direntry* pde;
	struct ufs_direntry *uden;
	struct ufs_direntry *nuden;
	uint8_t intree=0;
	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();

	// handle the case rename to a existing file. delete the file
	if ((ip = dir_lookup(dir_inode, newname,uden)) != NULL)  {
		//userfs_assert(strcmp(de->name, newname) == 0);
		if(uden==NULL){
			goto rename;
		}else{

		
			pde = uden->direntry;
			if(pde!=NULL){
				if(strcmp(pde->name, newname) == 0){
					printf("%s already exist\n", newname);
					return -1;
				}
			}
		}

		de_cache_del(dir_inode, pde->name);

		iput(ip);

		idealloc(ip);
	}


rename:
	dir_lookup(dir_inode, oldname,uden);
	if(uden!=NULL){
		intree=1;
		if(uden->addordel==Delden){
			printf("%s not exist\n", oldname);
			return -1;
		}
		pde=uden->direntry;
		struct fs_direntry pden;
		de_cache_del(dir_inode, oldname);
		userfs_assert(strlen(token) < DIRSIZ+8);
		struct timeval curtime;
		struct fuckfs_dentry *fdentry = (struct fuckfs_dentry *)malloc(sizeof(struct fuckfs_dentry));
		fdentry->entry_type=DIR_LOG;
		fdentry->ino=dir_inode->inum;
		fdentry->addordel= Delden;
		fdentry->invalid=1;	
		gettimeofday(&curtime,NULL);
		fdentry->mtime =curtime.tv_sec;
		strncpy(fdentry->name,oldname,DIRSIZ);
		fdentry->links_count=-1;
		append_entry_log(fdentry,1);

		pden.ino=pde->ino;
		strncpy(pden.name,oldname,DIRSIZ);
		uden->addordel=Delden;
		uden->direntry=&pden;
		
		if(intree==1){
			pthread_rwlock_wrlock(&dir_inode->art_rwlock);
			ret= art_delete(&dir_inode->dentry_tree, oldname,strlen(oldname));
			pthread_rwlock_unlock(&dir_inode->art_rwlock);
			if (!ret){
				panic("art_delete error\n");
				return -1;
			}
		}
		pthread_rwlock_wrlock(&dir_inode->art_rwlock);
		art_insert(&dir_inode->dentry_tree, oldname,strlen(oldname), uden);
		pthread_rwlock_unlock(&dir_inode->art_rwlock);
		// remove previous newname if exist.
		de_cache_del(dir_inode, newname);

		fdentry->addordel=Addden;
		fdentry->invalid=1;	
		fdentry->file_type=T_DIR;
		gettimeofday(&curtime,NULL);
		fdentry->mtime =curtime.tv_sec;
		strncpy(fdentry->name,newname,strlen(newname));
		fdentry->links_count=1;
		append_entry_log(fdentry,1);

		strncpy(pde->name,newname,strlen(newname));
		nuden =(struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
		nuden->direntry=pde;
		nuden->addordel=Addden;

		pthread_rwlock_wrlock(&dir_inode->art_rwlock);
		ret = art_insert(&dir_inode->dentry_tree, newname, strlen(newname), nuden);
		pthread_rwlock_unlock(&dir_inode->art_rwlock);
		ret = 0;
		goto direntry_found;
	}else{
		printf("%s not exist\n", oldname);
	}

direntry_found:

	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		g_perf_stats.dir_search_nr_miss++;
	}

	return ret;
}



int dir_remove_entry(struct inode *dir_inode, char *name, uint32_t inum)
{
	//userfs_printf("dir_remove_entry %s\n",name);
	offset_t off = 0;
	struct inode *ip;
	char token[DIRSIZ+8] = {0};
	uint32_t n;
	uint64_t tsc_begin, tsc_end;
	uint8_t intree=0,valid=0;
	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();

	struct fs_direntry* pde;
	int ret=0;
	
	pthread_rwlock_rdlock(&dir_inode->art_rwlock);
	struct ufs_direntry *uden=art_search(&dir_inode->dentry_tree, name,strlen(name));
	pthread_rwlock_unlock(&dir_inode->art_rwlock);
	
	if(uden == NULL && dir_inode->root>0 && dir_inode->inmem_den==0){
		uden=fetch_ondisk_dentry_from_kernel(dir_inode,name);
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
	
	}

	if(uden!=NULL){
		intree=1;

		if(uden->addordel==Delden){
			userfs_info("%s not exist\n", name);
			return -1;
		}

		uden->addordel=Delden;
		pde=uden->direntry;
		goto dirent_found;
	}else{
		userfs_info("file %s not exit\n", name);
		return 0;
	}

dirent_found:
	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		//g_perf_stats.dir_search_nr++;
	}

	dir_inode->size -= FUCKFS_DIR_REC_LEN(sizeof(name));


	userfs_assert(pde->ino != 0);
	
	ret=put_inode_bitmap(pde->ino);
	

	de_cache_del(dir_inode, name);

	userfs_debug("remove file %s from directory\n", name);

	struct setattr_logentry *attrentry = (struct setattr_logentry *)malloc(sizeof(struct setattr_logentry));
	attrentry->ino = pde->ino;
	attrentry->entry_type=SET_ATTR;
	attrentry->isdel=1;
	struct timeval curtime;
	gettimeofday(&curtime,NULL);
	attrentry->mtime=curtime.tv_sec;
	attrentry->ctime=curtime.tv_sec;
	append_entry_log(attrentry,0);

	struct fuckfs_dentry *fdentry = (struct fuckfs_dentry *)malloc(sizeof(struct fuckfs_dentry));
	fdentry->entry_type=DIR_LOG;
	
	fdentry->ino=dir_inode->inum;
	fdentry->name_len=strlen(name);               /* length of the dentry name */
	fdentry->file_type=T_DIR;              /* file type */
	fdentry->addordel=Delden;
	gettimeofday(&curtime,NULL);
	fdentry->mtime =curtime.tv_sec;
	strncpy(fdentry->name,name,DIRSIZ);
	fdentry->fino =pde->ino;
	
	
	if(intree==1){
		pthread_rwlock_wrlock(&dir_inode->art_rwlock);
		ret= art_delete(&dir_inode->dentry_tree, name,strlen(name));
		pthread_rwlock_unlock(&dir_inode->art_rwlock);
		if (!ret){
			userfs_info("%s not in dentry_tree\n", name);
			//panic("art_delete error\n");
			return -1;
		}
	}


	fdentry->invalid=1;	
	append_entry_log(fdentry,1);

	pthread_rwlock_wrlock(&dir_inode->art_rwlock);
	art_delete(&dir_inode->dentry_tree, name,strlen(name));
	art_insert(&dir_inode->dentry_tree, name,strlen(name),uden);
	pthread_rwlock_unlock(&dir_inode->art_rwlock);

	return 0;
}


// Paths
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode* namex(char *path, int parent, char *name)
{
	struct inode *ip, *next;

	if(memcmp(path, mountpoint, strlen(mountpoint))==0){
		path +=strlen(mountpoint);
		//printf("path %s\n", path);
	}
	
	if (*path == '/') 
		ip = iget(ROOTINO);
	else
		//ip = idup(proc->cwd);
		panic("relative path is not yet implemented\n");

	
	while ((path = get_next_name(path, name)) != 0) {
		ilock(ip);
		if (ip->itype != T_DIR){
			iunlockput(ip);
			return NULL;
		}

		if (parent && *path == '\0') {
			// Stop one level early.
			iunlock(ip);
			return ip;
		}
		if ((next = dir_lookup(ip, name, NULL)) == NULL) {
			//printf("in if\n");
			iunlockput(ip);
			return NULL;
		}
		iunlockput(ip);
		ip = next;
	}

	if (parent) {
		iput(ip);
		return NULL;
	}

	return ip;
}

struct inode* namei(char *path)
{
#if 0 // This is for debugging.
	struct inode *inode, *_inode;
	char name[DIRSIZ];

	_inode = dlookup_find(g_root_dev, path); 

	if (!_inode) {
		inode = namex(path, 0, name);
		if (inode)
			dlookup_alloc_add(g_root_dev, inode, path);
	} else {
		inode = namex(path, 0, name);
		userfs_assert(inode == _inode);
	}

	return inode;
#else
	struct inode *inode;
	char name[DIRSIZ];

	inode = dlookup_find(path); 
	if (inode && (inode->flags & I_DELETING)) 
		return NULL;

	if (!inode) {
		inode = namex(path, 0, name);
		if (inode)
			//userfs_printf("dlookup_alloc_add path %s\n",path);
			dlookup_alloc_add(inode, path);
	} else {
		inode->i_ref++;
	}
	
	return inode;
#endif
}

struct inode* nameiparent(char *path, char *name)
{
#if 0 // This is for debugging.
	struct inode *inode, *_inode;
	char parent_path[MAX_PATH];

	get_parent_path(path, parent_path);

	_inode = dlookup_find(g_root_dev, parent_path); 

	if (!_inode) {
		inode = namex(path, 1, name);
		if (inode)
			dlookup_alloc_add(g_root_dev, inode, parent_path);
		_inode = inode;
	} else {
		inode = namex(path, 1, name);
		userfs_assert(inode == _inode);
	}

	return inode;
#else
	struct inode *inode;
	char parent_path[MAX_PATH];
	get_parent_path(path, parent_path, name);
	inode = dlookup_find(parent_path);  
	if (inode && (inode->flags & I_DELETING)) 
		return NULL;

	if (!inode) {
		inode = namex(path, 1, name);   //
		if (inode){
		
			dlookup_alloc_add(inode, parent_path);
		}
	} else {
		inode->i_ref++;
	}

	return inode;
#endif
}

