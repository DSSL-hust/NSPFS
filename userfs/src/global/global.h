#ifndef __global_h__
#define __global_h__

#include "types.h"
#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t g_ssd_dev;
extern uint8_t g_log_dev;
extern uint8_t g_hdd_dev;

/**
 * Allocates all structures using the global values.
 */
void userfs_setup(void);

#define g_n_devices			4
#define g_root_dev			1
#define g_block_size_bytes	4096UL
#define g_lblock_size_bytes	2097152UL
#define g_block_size_shift	12UL
#define g_lblock_size_shift	21UL
#define lblock_addr 512UL

//#define FILENVMBLOCK 32
#define FILENVMBLOCK 64
// 2 is for microbenchmark
// 6 is generally too big. but Redis AOF mode requires at least 6.
#define g_max_blocks_per_operation 3
#define g_hdd_block_size_bytes 4194304UL
#define g_hdd_block_size_shift 22UL
#define g_directory_shift  16UL
#define g_directory_mask ((1 << ((sizeof(inum_t) * 8) - g_directory_shift)) - 1)
#define g_segsize_bytes    (1ULL << 30)  // 1 GB
#define g_max_read_cache_blocks  (2 << 20) // 8 GB

#define g_fd_start  1000000

#define NINODES 50000

#define ROOTINO 128  // root i-number

#define NDIRECT 7
#define NINDIRECT (g_block_size_bytes / sizeof(addr_t))

//#define N_FILE_PER_DIR (g_block_size_bytes / sizeof(struct userfs_dirent))
//#define g_max_open_files ((NDIRECT + 1) * N_FILE_PER_DIR)
#define g_max_open_files 10000
/* FIXME: compute this */
#define MAXFILE 10000000

// maximum size of full path
//#define MAX_PATH 4096
#define MAX_PATH 1024

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 32

#define SHM_START_ADDR (void *)0x7ff000000000UL
#define SHM_SIZE (200 << 20)
#define SHM_NAME "/userfs_shm"

#define mountpoint "/mnt/pmem_emul"

static uint64_t ssd_size[g_n_devices] = {25769803776UL, 500107862016UL, 512110190592UL, 250059350016UL};

static uint64_t FFF00 =~(((unsigned long)0x1<<g_block_size_shift) -1);
static uint64_t FF000 =~(((unsigned long)0x1<<g_block_size_shift) -1);
/**
 *
 * All global variables here are default set by global.c
 * but might be changed the .ini reader.
 * Please keep the "g_" prefix so we know what is global and
 * what isn't.
 *
 */
// extern uint	g_n_devices; /* old NDEV */
// extern uint	g_root_dev; /* old ROOTDEV */
// extern uint	g_max_open_files; /*old NFILE*/
// extern uint	g_block_size_bytes; /* block size in bytes, old BSIZE*/
// extern uint	g_max_blocks_per_operation; /* old MAXOPBLOCKS*/
// extern uint	g_log_size_blks; /* log size in blocks, old LOGSIZE*/
// extern uint	g_fs_size_blks; /* filesystem size in blocks, old FSSIZE*/
// extern uint	g_max_extent_size_blks; /* filesystem size in blocks, old MAX_EXTENT_SIZE*/
// extern uint	g_block_reservation_size_blks; /* reservation size in blocks, old BRESRV_SIZE*/
// extern uint	g_fd_start; /* offset start of fds used by userfs, old FD_START*/

#ifdef __cplusplus
}
#endif

#endif
