// to use O_DIRECT flag
//
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <libpmem.h>

#include "global/global.h"
#include "global/util.h"
#include "userfs/userfs_user.h"
#include "storage/storage.h"

#ifdef __cplusplus
extern "C" {
#endif



static inline void PERSISTENT_BARRIER(void)
{
	asm volatile ("sfence\n" : : );
}

///////////////////////////////////////////////////////

static uint8_t *dax_addr[g_n_devices + 1];
static size_t mapped_len[g_n_devices + 1];


uint8_t *dax_init(uint8_t dev, char *dev_path)
{
	int fd;

	fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cannot open dax device %s\n", dev_path);
		exit(-1);
	}

	
	dax_addr[dev] = (uint8_t *)mmap(NULL, dev_size[dev], PROT_READ | PROT_WRITE, MAP_SHARED| MAP_POPULATE, fd, 0);

	if (dax_addr[dev] == MAP_FAILED) {
		perror("cannot map file system file");
		exit(-1);
	}

	return dax_addr[dev];
}

int dax_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	memmove(buf, dax_addr[dev] + (blockno<<g_block_size_shift), io_size);

	return io_size;
}

int dax_read_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, 
		uint32_t io_size)
{

	memmove(buf, dax_addr[dev] + (blockno * g_block_size_bytes) + offset, 
			io_size);

	return io_size;
}


int dax_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	
	addr_t addr = (addr_t)dax_addr[dev] + (blockno << g_block_size_shift);

	
	pmem_memmove_persist((void *)addr, buf, io_size);
	
	PERSISTENT_BARRIER();
	return io_size;
}

int dax_write_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, 
		uint32_t io_size)
{
	
	addr_t addr = (addr_t)dax_addr[dev] + (blockno << g_block_size_shift) + offset;

	
	pmem_memmove_persist((void *)addr, buf, io_size);
	PERSISTENT_BARRIER();


	return io_size;
}

int dax_commit(uint8_t dev)
{
	return 0;
}

int dax_erase(uint8_t dev, addr_t blockno, uint32_t io_size)
{
	memset(dax_addr[dev] + (blockno * g_block_size_bytes), 0, io_size);

}

void dax_exit(uint8_t dev)
{
	munmap(dax_addr[dev], dev_size[dev]);

	return;
}

#ifdef __cplusplus
}
#endif
