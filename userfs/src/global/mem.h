#ifndef _MEM_H_
#define _MEM_H_

#include <stdlib.h>

#include "global/ncx_slab.h"

#ifdef __cplusplus
extern "C" {
#endif

// memory allocator wrapper.

static inline void *userfs_alloc(size_t size)
{
#ifdef USE_SLAB
	return ncx_slab_alloc(userfs_slab_pool, size);
#else
	return malloc(size);
#endif
}

static inline void *userfs_alloc_shared(size_t size)
{
	return ncx_slab_alloc(userfs_slab_pool_shared, size);
}

static inline void *userfs_zalloc(size_t size)
{
#ifdef USE_SLAB
	void *ret;
	ret = ncx_slab_alloc(userfs_slab_pool, size);
	memset(ret, 0, size);
	return ret;
#else
	return calloc(1, size);
#endif
}

static inline void userfs_free(void *ptr)
{
#ifdef USE_SLAB
	return ncx_slab_free(userfs_slab_pool, ptr);
#else
	return free(ptr);
#endif
}

static inline void userfs_free_shared(void *ptr)
{
	return ncx_slab_free(userfs_slab_pool_shared, ptr);
}

#ifdef __cplusplus
}
#endif

#endif
