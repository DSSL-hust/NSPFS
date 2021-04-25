#include "filesystem/fs.h"
#include "io/block_io.h"
//#include "log/log.h"

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
	//dir_block->log_version = fs_log->avail_version;

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
	//printf("get_dirent_block\n");
	int ret;
	struct buffer_head *bh;
	struct dirent_block *d_block;
	addr_t block_no;
	// Currently, assuming directory structures are stored in NVM.
	uint8_t dev = g_root_dev;

	userfs_assert(dir_inode->itype == T_DIR);
	//userfs_assert(offset <= dir_inode->size + sizeof(struct userfs_dirent));

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
		//printf("bmap_req.blk_count_found %d\n", bmap_req.blk_count_found);
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
	//userfs_printf("dir_lookup name %s, ino %d\n",name,dir_inode->inum);
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
	//userfs_printf("ip is %lu\n",ip);
	if (ip) {
		ip->i_ref++;
		if (enable_perf_stats) {
			g_perf_stats.dir_search_nr_hit++;
		}
		//userfs_printf("ip inum is %d, type is %d,ip->i_ref %d\n",ip->inum,ip->itype,ip->i_ref);
		return ip;
	}
	
/*
	printf("test art_search\n");
	char str[9]="00000001";
	uden=art_search(&dir_inode->dentry_tree, str,8);
	printf("str %s, uden %lu\n", str,uden);
	char str1[9]="00000002";
	uden=art_search(&dir_inode->dentry_tree, str1,8);
	printf("str %s, uden %lu\n", str1,uden);
	char str2[9]="00000003";
	uden=art_search(&dir_inode->dentry_tree, str2,8);
	printf("str %s, uden %lu\n", str2,uden);
	char str3[9]="00000004";
	uden=art_search(&dir_inode->dentry_tree, str3,8);
	printf("str %s, uden %lu\n", str3,uden);
*/
	//printf("dir_inode %d art_search %s\n",dir_inode->inum, name);
	//userfs_info("filename is %s, dir_inode->dentry_tree is %lu\n", name,&dir_inode->dentry_tree);
	pthread_rwlock_rdlock(&dir_inode->art_rwlock);
	uden=art_search(&dir_inode->dentry_tree, name,strlen(name));
	pthread_rwlock_unlock(&dir_inode->art_rwlock);
	//userfs_printf("uden %lu, dir_inode->root %lu\n", uden,dir_inode->root);

	if(uden == NULL && dir_inode->root>0 && dir_inode->inmem_den==0){
		//printf("fetch_ondisk_dentry_from_kernel\n");
		//uden=fetch_ondisk_dentry_from_kernel(dir_inode,name);
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
		//dir_inode->inmem_den=1;
		/*
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
		uden=art_search(&dir_inode->dentry_tree, name, strlen(name));
		*/
		//printf("2uden %lu\n", uden);
	}
	pthread_rwlock_rdlock(&dir_inode->art_rwlock);
	uden=art_search(&dir_inode->dentry_tree, name,strlen(name));
	pthread_rwlock_unlock(&dir_inode->art_rwlock);

	/*
	if(uden == NULL){
		uden=fetch_ondisk_dentry_from_kernel(dir_inode,name);
		//dir_inode->inmem_den=1;
		//fetch_ondisk_dentry(dir_inode);
		//dir_inode->inmem_den=1;
		//uden=art_search(&dir_inode->dentry_tree, name, strlen(name));
		//printf("2uden %lu\n", uden);
		userfs_info("uden is %lu\n",uden);
	}
	*/
	//userfs_printf("uden is %lu\n",uden);
	if(uden == NULL && dir_inode->root>0){
		//printf("fetch_ondisk_dentry_from_kernel\n");
		uden=fetch_ondisk_dentry_from_kernel(dir_inode,name);
	}
	//userfs_printf("uden is %lu\n",uden);
	if(uden!=NULL){
		//userfs_info("uden addordel %d\n",uden->addordel);
		if(uden->addordel==Delden){
			userfs_info("%s not exist\n", name);
			return NULL;
		}
		pde=uden->direntry;
		//userfs_printf("pde->ino %d, name %s\n",pde->ino,pde->name);
		inum = pde->ino;
		if(inum<=0){
			userfs_printf("inode num %d wrong\n", inum);
			return NULL;
		}
		ip = iget(inum);
		//printf("inum %d\n", inum);
		//printf("0ip->itype %d\n", ip->itype);
		ip->itype = pde->file_type;
		//printf("1ip->itype %d\n", ip->itype);
		//printf("1ip->flags %d\n", ip->flags);
		//printf("!(ip->flags & I_VALID) %d\n", !(ip->flags & I_VALID));
		userfs_assert(ip);
		//printf("userfs_assert(ip);\n");
		//*poff = 0;
		//printf("iput(ip)\n");
		
		//iput(ip);

		//printf("de_cache_alloc_add %s\n",name);
		de_cache_alloc_add(dir_inode, name, ip);
		//userfs_info("ip %lu, ip->flags %d, ip->i_ref %d\n", ip,ip->flags, ip->i_ref);
		return ip;

	}
/*	
	if(dir_inode->inmem_den==0)
	{
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
		uden=art_search(&dir_inode->dentry_tree, name, strlen(name));
	}
*/	
/*
	while(off<dir_inode->size){
		blk_base= get_dirent_block(dir_inode, off);   //bug
		pde=(struct fs_direntry*) blk_base;
		dlimit = blk_base + g_block_size_bytes;
		while ((char *)pde < dlimit) {
			/* this code is executed quadratically often */
			/* do minimal checking `by hand' */

/*			if (pde->ino == 0) {
				de_len = le16_to_cpu(pde->de_len);
				pde = (struct fs_direntry *)((char *)pde + de_len);
				continue;
			}
			printf("pde->name is %s, %d\n",pde->name,pde->ino);
			if (namecmp(pde->name, name) == 0) {
				if (poff) {
					*poff = off;
					userfs_assert(*poff <= dir_inode->size);
				}
				inum = pde->ino;
				uden = (struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
				//uden->logoffset=-1;
				//uden->generation=0;
				uden->addordel = Ondisk;
				uden->direntry=pde;
				art_insert(&dir_inode->dentry_tree, name,strlen(name),uden);

				ip = iget(inum);

				userfs_assert(ip);
				printf("ip->flags %d\n", ip->flags);
				printf("!(ip->flags & I_VALID) %d\n", !(ip->flags & I_VALID));
/*				if (!(ip->flags & I_VALID)) {
					//struct dinode dip;

					//read_ondisk_inode(dir_inode->dev, inum, &dip);
					// or icache search?

					//userfs_assert(dip.itype != 0);

					ip->i_sb = sb;
					//ip->_dinode = (struct dinode *)ip;
					//sync_inode_from_dinode(ip, &dip);
					ip->flags |= I_VALID;

					panic("Not a valid call path!\n");
				}
*/
/*				iput(ip);

				//de_cache_alloc_add(dir_inode, name, ip, off); 

				if (enable_perf_stats) {
					tsc_end = asm_rdtscp();
					g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
					g_perf_stats.dir_search_nr_miss++;
				}

				return ip;
			}
			de_len = le16_to_cpu(pde->de_len);
			pde = (struct fs_direntry *)((char *)pde + de_len);
		}
		off+=g_block_size_bytes;
	}

	if (poff)
		*poff = 0;
*/

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
		//userfs_assert(strcmp(pde->name, newname) == 0);

		//bitmap_clear(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

		//memset(de, 0, sizeof(*de));

		de_cache_del(dir_inode, pde->name);

		iput(ip);

		idealloc(ip);
	}

	//de = (struct userfs_dirent *)get_dirent_block(dir_inode, 0);
	//userfs_assert(de);

	
	//de = (struct userfs_dirent *)get_dirent_block(dir_inode, 0);
	//blk_base= get_dirent_block(dir_inode, 0);
	//struct fs_direntry* pde=(struct fs_direntry*) blk_base;
	

	//printf("dir_inode %d art_search %s\n",dir_inode->inum, oldname);
	//printf("filename is %s, namelen is %d\n", oldname,strlen(oldname));
	//pde=art_search(&dir_inode->dentry_tree, oldname,strlen(oldname));
	//printf("pde is %lu\n",pde);

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
		//printf("pde->ino %d, name %s\n",pde->ino,pde->name);
		de_cache_del(dir_inode, oldname);
		//memset(token, 0, DIRSIZ+8);
		//sprintf(token, "%s@%d", oldname, de->inum);
		userfs_assert(strlen(token) < DIRSIZ+8);
		//add_to_loghdr(L_TYPE_DIR_DEL, dir_inode, de->inum, 
		//		dir_inode->size, token, strlen(token));
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
		//printf("fdentry->ino %d\n", fdentry->ino);
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

		//strncpy(de->name, newname, DIRSIZ);
		//memset(token, 0, DIRSIZ+8);
		//sprintf(token, "%s@%d", newname, de->inum);
		//userfs_assert(strlen(token) < DIRSIZ+8);
		//add_to_loghdr(L_TYPE_DIR_RENAME, dir_inode, de->inum, 
		//		dir_inode->size, token, strlen(token));
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
		//nuden->logoffset=logoffset;
		nuden->addordel=Addden;

		pthread_rwlock_wrlock(&dir_inode->art_rwlock);
		ret = art_insert(&dir_inode->dentry_tree, newname, strlen(newname), nuden);
		pthread_rwlock_unlock(&dir_inode->art_rwlock);
/*		
		if(!ret){
			panic("art insert error\n");
			return -1;
		}
*/
		ret = 0;
		goto direntry_found;
	}else{
		printf("%s not exist\n", oldname);
	}
/*
	while(off<dir_inode->size){
		blk_base= get_dirent_block(dir_inode, off);
		pde=(struct fs_direntry*) blk_base;
		dlimit = blk_base + g_block_size_bytes;
		while ((char *)pde < dlimit) {
			// this code is executed quadratically often 
			// do minimal checking `by hand' 

		
			printf("pde->name,file_type is %s, %d, %d\n",pde->name,pde->ino,pde->file_type);
			if (namecmp(pde->name, oldname) == 0) {
				de_cache_del(dir_inode, oldname);
				//memset(token, 0, DIRSIZ+8);
				//sprintf(token, "%s@%d", oldname, de->inum);
				userfs_assert(strlen(token) < DIRSIZ+8);
				//add_to_loghdr(L_TYPE_DIR_DEL, dir_inode, de->inum, 
				//		dir_inode->size, token, strlen(token));
				struct timeval curtime;
				struct fuckfs_dentry *fdentry = (struct fuckfs_dentry *)malloc(sizeof(struct fuckfs_dentry));
				fdentry->entry_type=DIR_LOG;
				fdentry->ino=dir_inode->inum;
				fdentry->invalid=0;	
				gettimeofday(&curtime,NULL);
				fdentry->mtime =curtime.tv_sec;
				strncpy(fdentry->name,oldname,DIRSIZ);
				fdentry->links_count=-1;
				append_entry_log(fdentry,1);
				
				uden->direntry=pde;
				uden->logoffset=logoffset;
				uden->generation=0;
				uden->addordel=Delden;
				art_insert(&dir_inode->dentry_tree, oldname,strlen(oldname),uden);
/*
				ret=art_delete(&dir_inode->dentry_tree, oldname,strlen(oldname));
				if (!ret){
					panic("art_delete error\n");
					return -1;
				}
*/
/*				// remove previous newname if exist.
				de_cache_del(dir_inode, newname);

				//strncpy(de->name, newname, DIRSIZ);
				//memset(token, 0, DIRSIZ+8);
				//sprintf(token, "%s@%d", newname, de->inum);
				//userfs_assert(strlen(token) < DIRSIZ+8);
				//add_to_loghdr(L_TYPE_DIR_RENAME, dir_inode, de->inum, 
				//		dir_inode->size, token, strlen(token));

				fdentry->invalid=1;	
				gettimeofday(&curtime,NULL);
				fdentry->mtime =curtime.tv_sec;
				strncpy(fdentry->name,newname,DIRSIZ);
				fdentry->links_count=1;
				append_entry_log(fdentry,1);
				
				strncpy(pde->name,newname,strlen(newname));
				uden->direntry=pde;
				uden->logoffset=logoffset;
				uden->generation=0;
				uden->addordel=Addden;
				ret = art_insert(&dir_inode->dentry_tree, newname, strlen(newname), uden);
				/*
				if(!ret){
					panic("art insert error\n");
					return -1;
				}
				*/
/*				ret = 0;

				break;
			}
			de_len = le16_to_cpu(pde->de_len);
			pde = (struct fs_direntry *)((char *)pde + de_len);
		}
		off+=g_block_size_bytes;
	}
	*/
/*
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct userfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (namecmp(de->name, oldname) == 0) {
			/*
			if (add_to_log(dir_inode, dirent_array, 0, dir_inode->size) 
					!= dir_inode->size)
				panic("cannot write to log");
			iupdate(dir_inode);
			*/
			
/*			de_cache_del(dir_inode, oldname);
			//memset(token, 0, DIRSIZ+8);
			//sprintf(token, "%s@%d", oldname, de->inum);
			userfs_assert(strlen(token) < DIRSIZ+8);
			//add_to_loghdr(L_TYPE_DIR_DEL, dir_inode, de->inum, 
			//		dir_inode->size, token, strlen(token));
			struct timeval curtime;
			struct fuckfs_dentry *fdentry = (struct fuckfs_dentry *)malloc(sizeof(struct fuckfs_dentry));
			fdentry->entry_type=DIR_LOG;
			fdentry->ino=dir_inode->inum;
			fdentry->invalid=0;	
			gettimeofday(&curtime,NULL);
			fdentry->mtime =curtime.tv_sec;
			strncpy(fdentry->name,oldname,DIRSIZ);
			fdentry->links_count=-1;
			append_entry_log(fdentry,1);

			// remove previous newname if exist.
			de_cache_del(dir_inode, newname);

			strncpy(de->name, newname, DIRSIZ);
			memset(token, 0, DIRSIZ+8);
			sprintf(token, "%s@%d", newname, de->inum);
			userfs_assert(strlen(token) < DIRSIZ+8);
			//add_to_loghdr(L_TYPE_DIR_RENAME, dir_inode, de->inum, 
			//		dir_inode->size, token, strlen(token));

			fdentry->invalid=1;	
			gettimeofday(&curtime,NULL);
			fdentry->mtime =curtime.tv_sec;
			strncpy(fdentry->name,oldname,DIRSIZ);
			fdentry->links_count=1;
			append_entry_log(fdentry,1);
			
			ret = 0;

			break;
		}

		de++;
	}
*/
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
	//struct userfs_dirent *de;
	struct inode *ip;
	char token[DIRSIZ+8] = {0};
	uint32_t n;
	uint64_t tsc_begin, tsc_end;
	uint8_t intree=0,valid=0;
	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();
	
	/*
	if (dir_inode->size > g_block_size_bytes) {
		de_cache_find(dir_inode, name, &off); 

		if (off != 0) {
			de = (struct userfs_dirent *)get_dirent_block(dir_inode, off); 
			de += ((off % g_block_size_bytes) / sizeof(*de));

			 if (namecmp(de->name, name) == 0)
				 goto dirent_found;
		}
	}
	*/

	struct fs_direntry* pde;
	int ret=0;
	//de = (struct userfs_dirent *)get_dirent_block(dir_inode, 0);
	//blk_base= get_dirent_block(dir_inode, 0);
	//pde=(struct fs_direntry*) blk_base;
	
	//userfs_info("dir_inode %d art_search %s\n",dir_inode->inum, name);
	//printf("filename is %s, namelen is %d\n", name,strlen(name));
	pthread_rwlock_rdlock(&dir_inode->art_rwlock);
	struct ufs_direntry *uden=art_search(&dir_inode->dentry_tree, name,strlen(name));
	pthread_rwlock_unlock(&dir_inode->art_rwlock);
	//userfs_info("uden %lu\n",uden);

	//if(uden == NULL && dir_inode->inmem_den==0 &&dir_inode->root>0){
	if(uden == NULL && dir_inode->root>0 && dir_inode->inmem_den==0){
	//if(uden == NULL && dir_inode->root>0){
		//printf("fetch_ondisk_dentry_from_kernel\n");
		uden=fetch_ondisk_dentry_from_kernel(dir_inode,name);
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
		//dir_inode->inmem_den=1;
		/*
		fetch_ondisk_dentry(dir_inode);
		dir_inode->inmem_den=1;
		uden=art_search(&dir_inode->dentry_tree, name, strlen(name));
		*/
		//printf("2uden %lu\n", uden);
	}

	//pde=uden->direntry;
	//userfs_info("uden is %lu\n",uden);
	if(uden!=NULL){
		intree=1;

		if(uden->addordel==Delden){
			userfs_info("%s not exist\n", name);
			return -1;
		}

		uden->addordel=Delden;
		pde=uden->direntry;
		//userfs_info("uden->pde->ino %d, name %s\n",pde->ino,pde->name);
		goto dirent_found;
	}else{
		userfs_info("file %s not exit\n", name);
		return 0;
	}
/*
	printf("dir_inode->size is %d\n", dir_inode->size);
	if(dir_inode->size==0 || dir_inode->height==0){
		struct tmpinode *tinode =(struct tmpinode *)malloc(sizeof(struct tmpinode));
		ret=syscall(328, NULL, tinode, dir_inode->inum, 0, 0 );
		//ret=syscall(328, filename, tinode, 0, 1, 0 );
		if(ret<0){
			panic("syscall fetch inode error\n");
		}
		printf("tinode root is %d\n",tinode->root);
		printf("tinode height is %d\n",tinode->height);
		dir_inode->root=tinode->root;
		dir_inode->height=tinode->height;
		dir_inode->size = tinode->i_size;
	}

	while(off<dir_inode->size){
		blk_base= get_dirent_block(dir_inode, off);
		pde=(struct fs_direntry*) blk_base;
		dlimit = blk_base + g_block_size_bytes;
		while ((char *)pde < dlimit) {
			/* this code is executed quadratically often */
			/* do minimal checking `by hand' */

		
/*			printf("pde->name, file_type is %s, %d, %d\n",pde->name,pde->ino,pde->file_type);
			if (namecmp(pde->name, name) == 0) {
				userfs_assert(inum == pde->ino);
				uden=(struct ufs_direntry *)malloc(sizeof(struct ufs_direntry));
				uden->direntry=pde;
				//uden->logoffset=logoffset;
				//uden->generation=0;
				uden->addordel=Delden;
				goto dirent_found;
			}
			de_len = le16_to_cpu(pde->de_len);
			if (de_len <= 0)
				return -1;
			pde = (struct fs_direntry *)((char *)pde + de_len);
		}
		off+=g_block_size_bytes;
	}
*/
/*	userfs_assert(de);
	printf("de->name is %s, %d\n",de->name,de->inum);
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct userfs_dirent *)get_dirent_block(dir_inode, off);
		}
		printf("de->name is %s, %d\n",de->name,de->inum);
		if (namecmp(de->name, name) == 0) {
			userfs_assert(inum == de->inum);
			break;
		}

		de++;
	}
*/
dirent_found:
	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		//g_perf_stats.dir_search_nr++;
	}

	//if (dir_inode->size == off)
	//printf("dir_inode->size is %d\n", dir_inode->size);
	dir_inode->size -= FUCKFS_DIR_REC_LEN(sizeof(name));

	//bitmap_clear(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

	//sprintf(token, "%s@%d", name, inum);
	userfs_assert(pde->ino != 0);
	//userfs_info("pde->ino %d\n", pde->ino);
	
	ret=put_inode_bitmap(pde->ino);
	//userfs_assert(ret != 0);
	

	//add_to_loghdr(L_TYPE_DIR_DEL, dir_inode, de->inum, 
	//		dir_inode->size, token, strlen(token));

	//memset(de, 0, sizeof(*de));

	de_cache_del(dir_inode, name);

	userfs_debug("remove file %s from directory\n", name);
	//userfs_info("remove file %s from directory\n", name);

	struct setattr_logentry *attrentry = (struct setattr_logentry *)malloc(sizeof(struct setattr_logentry));
	attrentry->ino = pde->ino;
	attrentry->entry_type=SET_ATTR;
	attrentry->isdel=1;
	struct timeval curtime;
	gettimeofday(&curtime,NULL);
	attrentry->mtime=curtime.tv_sec;
	attrentry->ctime=curtime.tv_sec;
	append_entry_log(attrentry,0);
	//printf("append_entry_log\n");

	struct fuckfs_dentry *fdentry = (struct fuckfs_dentry *)malloc(sizeof(struct fuckfs_dentry));
	fdentry->entry_type=DIR_LOG;
	
	fdentry->ino=dir_inode->inum;
	fdentry->name_len=strlen(name);               /* length of the dentry name */
	fdentry->file_type=T_DIR;              /* file type */
	fdentry->addordel=Delden;
	//printf("fdentry\n");
	gettimeofday(&curtime,NULL);
	fdentry->mtime =curtime.tv_sec;
	strncpy(fdentry->name,name,DIRSIZ);
	fdentry->fino =pde->ino;
	
	
	//userfs_info("intree %d\n",intree);
	if(intree==1){
		pthread_rwlock_wrlock(&dir_inode->art_rwlock);
		ret= art_delete(&dir_inode->dentry_tree, name,strlen(name));
		pthread_rwlock_unlock(&dir_inode->art_rwlock);
		//printf("art_delete %d\n", ret);
		if (!ret){
			userfs_info("%s not in dentry_tree\n", name);
			//panic("art_delete error\n");
			return -1;
		}
	}

	//printf("fdentry->ino %d\n", fdentry->ino);
	//userfs_info("fdentry->fino %d\n", fdentry->fino);
	fdentry->invalid=1;	
	append_entry_log(fdentry,1);

	pthread_rwlock_wrlock(&dir_inode->art_rwlock);
	art_delete(&dir_inode->dentry_tree, name,strlen(name));
	art_insert(&dir_inode->dentry_tree, name,strlen(name),uden);
	pthread_rwlock_unlock(&dir_inode->art_rwlock);

	//userfs_info("uden %lu, uden->addordel %d\n",uden,uden->addordel);
	/* unoptimized code.

	   if (add_to_log(dir_inode, dirent_array, 0, dir_inode->size) 
	   != dir_inode->size)
	   panic("cannot write to log");

	   iupdate(dir_inode);
   */

	//userfs_info("dir_remove_entry success %s\n",name);
	return 0;
}

/*
// Write a new directory entry (name, inum) into the directory inode.
int dir_add_entry(struct inode *dir_inode, char *name, uint32_t inum)
{
	printf("dir_add_entry\n");
	offset_t off = 0;
	struct userfs_dirent *de;
	struct inode *ip;
	char token[DIRSIZ+8] = {0};
	uint32_t n, next_avail_slot;
	uint64_t tsc_begin, tsc_end;

	// Check that name is not present.
	/*
	if ((ip = dir_lookup(dir_inode, name, 0)) != NULL) {
		iput(ip);
		return -EEXIST;
	}
	*/
/*
	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();
	printf("dir_add_entry1\n");
	next_avail_slot = find_next_zero_bit(dir_inode->dirent_bitmap,
			DIRBITMAP_SIZE, 0);
	
	printf("dir_add_entry2\n");
	off = next_avail_slot * sizeof(*de);
	//de = (struct userfs_dirent *)get_dirent_block(dir_inode, off);
	de += ((off % g_block_size_bytes) / sizeof(*de));

	if (de->inum == 0) 
		goto empty_found;
	printf("dir_add_entry3\n");
search_slot:
	//de = (struct userfs_dirent *)get_dirent_block(dir_inode, 0);
	userfs_assert(de);

	// brute-force search of empty dirent slot.
	for (off = 0, n = 0; off < dir_inode->size + sizeof(*de); off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			//de = (struct userfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0)
			break;

		de++;
		bitmap_set(dir_inode->dirent_bitmap, off / sizeof(*de), 1);
	}

empty_found:
	bitmap_set(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		//g_perf_stats.dir_search_nr++;
	}

	strncpy(de->name, name, DIRSIZ);
	de->inum = inum;

	//dir_inode->size += sizeof(struct userfs_dirent);
	// directory inode size is a max offset.
	if (off + sizeof(struct userfs_dirent) > dir_inode->size)  
		dir_inode->size = off + sizeof(struct userfs_dirent);

	de_cache_alloc_add(dir_inode, name, 
			icache_find(inum), off);

	userfs_get_time(&dir_inode->mtime);

	printf("name %s inum %u off %lu\n", name, inum, off);

	memset(token, 0, DIRSIZ+8);
	sprintf(token, "%s@%d", name, inum);

	//add_to_loghdr(L_TYPE_DIR_ADD, dir_inode, inum, 
	//		dir_inode->size, token, strlen(token));

	/*
	if (add_to_log(dir_inode, dirent_array, 0, dir_inode->size) 
			!= dir_inode->size)
		panic("cannot write to log");
	*/
/*
	// optimization: It is OK to skip logging dir_inode
	// Kernfs can update mtime (from logheader) and size (from dirent_array).
	// iupdate(dir_inode);

	return 0;
}



static struct inode* namen(char *path, int parent, char *name)
{
	printf("--------------------\n");
	printf("namex path %s, name %s\n",path,name);
	struct inode *ip, *next;
	//char fpath[DIRSIZ];
	//printf("mountpoint %s\n", mountpoint);

	//strncpy(fpath, path, strlen(mountpoint));
	//printf("fpath %s\n", fpath);

	//printf("memcmp(path, mountpoint):%d\n", memcmp(path, mountpoint, strlen(mountpoint)));
	if(memcmp(path, mountpoint, strlen(mountpoint))==0){
		path +=strlen(mountpoint);
		printf("path %s\n", path);
	}
	

	if (*path == '/') 
		ip = iget(ROOTINO);
	else
		//ip = idup(proc->cwd);
		panic("relative path is not yet implemented\n");

	printf("ip->inum is %d\n", ip->inum);
	printf("ip->itype is %d\n", ip->itype);
	// directory walking of a given path
	while ((path = get_next_name(path, name)) != 0) {
		printf("namex path %s, name %s\n",path,name);
		ilock(ip);
		printf("ip->itype is %d\n", ip->itype);
		if (ip->itype != T_DIR){
			iunlockput(ip);
			return NULL;
		}
		printf("if %d\n",(parent && *path == '\0'));

		if (parent && *path == '\0') {
			// Stop one level early.
			iunlock(ip);
			return ip;
		}
		printf("dir_lookup %s\n",name);
		if ((next = dir_lookup(ip, name, 0, NULL)) == NULL) {
			printf("in if\n");
			iunlockput(ip);
			return NULL;
		}
		printf("iunlockput(ip)\n");
		iunlockput(ip);
		ip = next;
	}

	if (parent) {
		iput(ip);
		return NULL;
	}

	printf("inum %u - refcount %d\n", ip->inum, ip->i_ref);
	return ip;
}
*/

// Paths
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode* namex(char *path, int parent, char *name)
{
	//printf("--------------------\n");
	//userfs_printf("namex path %s\n",path);
	struct inode *ip, *next;
	//char fpath[DIRSIZ];
	//printf("mountpoint %s\n", mountpoint);

	//strncpy(fpath, path, strlen(mountpoint));
	//printf("fpath %s\n", fpath);

	//printf("memcmp(path, mountpoint):%d\n", memcmp(path, mountpoint, strlen(mountpoint)));
	if(memcmp(path, mountpoint, strlen(mountpoint))==0){
		path +=strlen(mountpoint);
		//printf("path %s\n", path);
	}
	
	printf("path %s\n", path);
	if (*path == '/') 
		ip = iget(ROOTINO);
	else
		//ip = idup(proc->cwd);
		panic("relative path is not yet implemented\n");

	//userfs_info("ip->inum is %d\n", ip->inum);
	//printf("ip->itype is %d\n", ip->itype);
	// directory walking of a given path
	while ((path = get_next_name(path, name)) != 0) {
		//userfs_info("namex path %s, name %s\n",path,name);
		ilock(ip);
		//userfs_info("ip->itype is %d\n", ip->itype);
		if (ip->itype != T_DIR){
			iunlockput(ip);
			return NULL;
		}
		//userfs_info("if %d\n",(parent && *path == '\0'));

		if (parent && *path == '\0') {
			// Stop one level early.
			iunlock(ip);
			return ip;
		}
		//userfs_info("dir_lookup %s\n",name);
		if ((next = dir_lookup(ip, name, NULL)) == NULL) {
			//printf("in if\n");
			iunlockput(ip);
			return NULL;
		}
		//printf("iunlockput(ip)\n");
		iunlockput(ip);
		ip = next;
	}

	if (parent) {
		iput(ip);
		return NULL;
	}

	//userfs_printf("inum %u - refcount %d\n", ip->inum, ip->i_ref);
	return ip;
}

struct inode* namei(char *path)
{
	//userfs_printf("namei %s\n", path);
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
	//userfs_printf("inode %lu\n", inode);
	if (inode && (inode->flags & I_DELETING)) 
		return NULL;

	if (!inode) {
		inode = namex(path, 0, name);
		if (inode)
			//userfs_printf("dlookup_alloc_add path %s\n",path);
			dlookup_alloc_add(inode, path);
	} else {
		inode->i_ref++;
	/*	printf("kh_init\n");
		printf("inode->fcache_hash %lu\n", inode->fcache_hash);
		if(!inode->fcache_hash){
			inode->fcache_hash = kh_init(fcache);
			printf("inode->fcache_hash %lu, %d \n", inode->fcache_hash,kh_end(inode->fcache_hash));
		}
		*/
	}
	
	return inode;
#endif
}

struct inode* nameiparent(char *path, char *name)
{
	//printf("nameiparent\n");
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
	//printf("nameiparent\n");
	get_parent_path(path, parent_path, name);
	//userfs_info("nameiparent path %s, parent_path %s, name %s\n",path,parent_path,name);
	inode = dlookup_find(parent_path);  //在hash table中查找inode
	//printf("nameiparent parent_path %s, inode %lu\n",parent_path,inode);
	//printf("inode inum is %d\n", inode->inum);
	/*
	if(inode){
		userfs_info("inode flags is %d\n", inode->flags);
		userfs_info("inode %lu, (inode->flags & I_DELETING) %d\n",inode,(inode->flags & I_DELETING));
	}
	*/
	if (inode && (inode->flags & I_DELETING)) 
		return NULL;
//  理论上父目录不在hash中，应该去设备中查询数据
	if (!inode) {
		//userfs_info("namex %s\n",path);
		inode = namex(path, 1, name);   //
		if (inode){
			//userfs_info("dlookup_alloc_add parent_path %s\n",parent_path);
			dlookup_alloc_add(inode, parent_path);
		}
	} else {
		inode->i_ref++;
	}
//
	//userfs_info("inode %lu\n", inode);
	return inode;
#endif
}

/////////////////////////////////////////////////////////////////
// functions for debugging. Usually called by gdb.
// Note that I assume following functions are call when
// program is frozen by gdb so functions are not thread-safe.
//

/*
// place holder for python gdb module to records.
void dbg_save_inode(struct inode *inode, char *name)
{
	return ;
}

// place holder for python gdb module to records.
void dbg_save_dentry(struct userfs_dirent *de, struct inode *dir_inode)
{
	return ;
}

void dbg_dump_dir( uint32_t inum)
{
	offset_t off;
	struct userfs_dirent *de;
	struct inode *dir_inode;
	uint32_t n;

	dir_inode = iget(inum);

	if (!(dir_inode->flags & I_VALID)) {
		//struct dinode dip;
		int ret;

		//read_ondisk_inode(dir_inode->dev, inum, &dip);

		//if(dip.itype == 0) {
			userfs_info("inum %d does not exist\n", inum);
			return;
		//}

		//dir_inode->_dinode = (struct dinode *)dir_inode;
		//sync_inode_from_dinode(dir_inode, &dip);
		//dir_inode->flags |= I_VALID;
	}

	if (dir_inode->itype != T_DIR) {
		userfs_info("%s\n", "lookup for non DIR");
		//iput(dir_inode);
		return;
	}

	//de = (struct userfs_dirent *)get_dirent_block(dir_inode, 0);
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			//de = (struct userfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0) {
			de++;
			continue;
		}

		userfs_info("name %s\tinum %d\toff %lu\n", de->name, de->inum, off);
		de++;
	}

	userfs_info("%s\n", "--------------------------------");

	//iput(dir_inode);

	return;
}

// Walking through to path and snapshoting dentry and inode.
void dbg_path_walk(char *path)
{
	struct inode *inode, *next_inode;
	char name[DIRSIZ];

	if (*path != '/')
		return;

	inode = iget(ROOTINO);

	dbg_save_inode(inode, (char *)"/");

	while ((path = get_next_name(path, name)) != 0) {
		next_inode = dir_lookup(inode, name, 0);

		dbg_save_inode(inode, name);

		if (next_inode == NULL)
			break;

		inode = next_inode;
	}

	return ;
}

void dbg_dump_inode(uint32_t inum)
{
	struct inode *inode;

	inode = icache_find(inum);

	if (!inode) {
		userfs_info("cannot find inode %u\n", inum);
		return;
	}

	if (!(inode->flags & I_VALID)) {
		//struct dinode dip;
		//int ret;

		//read_ondisk_inode(inode->dev, inum, &dip);

		//if(dip.itype == 0) {
		//	userfs_info("inum %d does not exist\n", inum);
			//iput(inode);
		//	return;
		//}

		//inode->_dinode = (struct dinode *)inode;
		//sync_inode_from_dinode(inode, &dip);
		//inode->flags |= I_VALID;
	}

	userfs_info("inum %d type %d size %lu flags %u\n",
			inode->inum, inode->itype, inode->size, inode->flags);

	//iput(inode);
	return;
}

void dbg_check_inode(void *data)
{
	uint32_t i;
	//struct dinode *dinode;

	//for (i = 0; i < IPB ; i++) {
		//dinode = (struct dinode *)data + i;

		//if (dinode->itype > 3)
		//	GDB_TRAP;
	//}

	return;
}

void dbg_check_dir(void *data)
{
	uint32_t i;
	struct userfs_dirent *de;

	for (i = 0; i < (g_block_size_bytes / sizeof(struct userfs_dirent)) ; i++) {
		de = (struct userfs_dirent *)data + i;

		if (de->inum == 0)
			continue;

		userfs_info("%u (offset %lu): %s - inum %d\n", 
				i, i * sizeof(*de), de->name, de->inum);
	}
	
	return;
}
*/