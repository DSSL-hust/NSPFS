#include "ds/list.h"

typedef struct lru_options_t
{
	size_t num_slot;
	size_t node_size;
}lru_options_t;

typedef struct lru_node_t
{
	void* key;
	size_t key_len;

	unsigned long ino;
	//time_t ts;
	struct list_head hash_list;
	struct list_head lru_list;
}lru_node_t;

typedef struct inode_lru_t
{
	lru_options_t options;
	struct list_head* hash_slots;

	struct list_head  lru_nodes;
	size_t num_nodes;
}inode_lru_t;

inode_lru_t* malloc_lru(const lru_options_t* options);

//int copy_lru_node_data(lru_t* lru, const void* key, size_t key_len, unsigned long ino, int* expired);
int insert_lru_node(inode_lru_t* lru, const void* key, size_t key_len, unsigned long ino); 
int del_lru_node(inode_lru_t* lru, const void* key, size_t key_len);
