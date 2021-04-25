#ifndef _SYNC_H_
#define _SYNC_H_

#ifdef __cplusplus
extern "C" {
#endif

// Synchronization primitives for libFS
// This part heavily depends on APIs provided by low level APIs
// This implementation uses pthread APIs

#include "global/global.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/futex.h>

/* Check whether false sharing could be happened */
#define LOCK_NAME_SIZE 20
struct userfs_spinlock {
	int lock;	// lock variable
	char name[LOCK_NAME_SIZE]; // Name of lock (for debugging).
};

union userfs_mutex
{
	uint32_t u;
	struct state {
		uint8_t locked;
		uint8_t contended;
	}b;
};

typedef union userfs_mutex userfs_mutex_t;

struct userfs_condvar {
	userfs_mutex_t *m;		// pointer for mutex lock variable.
	int seq;	//sequence lock.
	// pad?
};

typedef struct userfs_condvar userfs_condvar_t;
	
#define CONDVAR_NAME_SIZE 20
struct cond_channel {
	char name[CONDVAR_NAME_SIZE];
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

// abstract APIs of conditional variable for fs/log/block layers to use.
void userfs_wait(volatile int *futex_addr);
void userfs_wakeup(volatile int *futex_addr);

// version using pthread
void userfs_cond_lock(void *channel);
void userfs_cond_unlock(void *channel);
void userfs_cond_wait(void *channel);
void userfs_cond_signal(void *channel);

// lightweight version
// spinlock
void userfs_spinlock_init(struct userfs_spinlock *lk, char *name);
void userfs_spin_lock(struct userfs_spinlock*);
void userfs_spin_unlock(struct userfs_spinlock*);

// mutex
int userfs_mutex_init(userfs_mutex_t *mu);
int userfs_mutex_destroy(userfs_mutex_t *mu);
int userfs_mutex_lock(userfs_mutex_t *mu);
int userfs_mutex_trylock(userfs_mutex_t *mu);
int userfs_mutex_unlock(userfs_mutex_t *mu);

// conditional variable
int userfs_condvar_init(struct userfs_condvar *cv);
int userfs_condvar_destroy(struct userfs_condvar *cv);
int userfs_condvar_signal(struct userfs_condvar *cv);
int userfs_condvar_broadcast(struct userfs_condvar *cv);
int userfs_condvar_wait(struct userfs_condvar *cv, userfs_mutex_t *mu);

#define m_barrier() asm volatile("": : :"memory")

/* Pause instruction to prevent excess processor bus usage */ 
#define cpu_relax() asm volatile("pause\n": : :"memory")

#define atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define cmpxchg(P, O, N) __sync_val_compare_and_swap((P), (O), (N))
#define atomic_inc(P) __sync_add_and_fetch((P), 1)
#define atomic_dec(P) __sync_add_and_fetch((P), -1) 
#define atomic_add(P, V) __sync_add_and_fetch((P), (V))
#define atomic_set_bit(P, V) __sync_or_and_fetch((P), 1<<(V))
#define atomic_clear_bit(P, V) __sync_and_and_fetch((P), ~(1<<(V)))

/*
inline int sys_futex(void *addr1, int op, int val1, 
		struct timespec *timeout, void *addr2, int val3)
{
	return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}
*/
#define sys_futex(addr1, op, val1, timeout, addr2, val3) \
	syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);

/* Atomic exchange (of various sizes) */
static inline void *xchg_64(void *ptr, void *x)
{
	__asm__ __volatile__("xchgq %0,%1"
			:"=r" ((unsigned long long) x)
			:"m" (*(volatile long long *)ptr), "0" ((unsigned long long) x)
			:"memory");

	return x;
}

static inline unsigned xchg_32(void *ptr, unsigned x)
{
	__asm__ __volatile__("xchgl %0,%1"
			:"=r" ((unsigned) x)
			:"m" (*(volatile unsigned *)ptr), "0" (x)
			:"memory");

	return x;
}

static inline unsigned short xchg_16(void *ptr, unsigned short x)
{
	__asm__ __volatile__("xchgw %0,%1"
			:"=r" ((unsigned short) x)
			:"m" (*(volatile unsigned short *)ptr), "0" (x)
			:"memory");

	return x;
}

static inline unsigned char xchg_8(void *ptr, unsigned char x)
{
	__asm__ __volatile__("xchgb %0,%1"
			:"=r" ((unsigned char) x)
			:"m" (*(volatile unsigned char*)ptr), "0" (x)
			:"memory");

	return x;
}

/* Test and set a bit */
static inline char atomic_bitsetandtest(void *ptr, int x)
{
	char out;
	__asm__ __volatile__("lock; bts %2,%1\n"
			"sbb %0,%0\n"
			:"=r" (out), "=m" (*(volatile long long *)ptr)
			:"Ir" (x)
			:"memory");

	return out;
}

#ifdef __cplusplus
}
#endif

#endif
