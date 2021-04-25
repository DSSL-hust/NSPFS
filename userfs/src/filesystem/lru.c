#include "lru.h"
#include<stddef.h>
//inode_lru_t* malloc_lru(const lru_options_t* options);

//int copy_lru_node_data(lru_t* lru, const void* key, size_t key_len, unsigned long ino, int* expired);
//int insert_lru_node(inode_lru_t* lru, const void* key, size_t key_len, unsigned long ino); 
//int del_lru_node(inode_lru_t* lru, const void* key, size_t key_len);

#define container_of(ptr, type, member) ( { \
const typeof( ((type *)0)->member ) *__mptr = (ptr); \
(type *)( (char *)__mptr - offsetof(type,member) ); } )

#define list_entry(ptr, type, member) container_of(ptr, type, member)

size_t hashname(const char* key, size_t key_len){
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned long hash = 0;
	int i;

	for (i = 0; i < key_len; i++) {
		hash = hash * seed + (*key++);
	}

	return hash;
}
/*
	size_t i = 0;
	unsigned int hash = 0;
	while (i != key_len) {
		hash += key[i++];
		hash += hash << 10;
		hash ^= hash >> 6;
	}
	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;
	return hash;
}*/

inode_lru_t* malloc_lru(const lru_options_t* options)
{
	if(!options){
		return NULL;
	}

	inode_lru_t* lru = (inode_lru_t*)calloc(1, sizeof(inode_lru_t));
	if(!lru){
		return NULL;
	}

	memcpy(&(lru->options), options, sizeof(lru_options_t));
	lru->hash_slots = (struct list_head*)calloc(options->num_slot, sizeof(struct list_head));
	for(size_t i = 0; i < options->num_slot; ++i){
		INIT_LIST_HEAD(lru->hash_slots+i);
	}

	INIT_LIST_HEAD(&(lru->lru_nodes));
	return lru;
}

static lru_node_t* new_or_expire_lru_node(inode_lru_t* lru, const void* key, size_t key_len)
{
	lru_node_t* node = NULL;
	if(lru->num_nodes >= lru->options.node_size){
		struct list_head* head = &(lru->lru_nodes);

		node = list_entry(head,lru_node_t, lru_list);
		node = list_next_entry(node,lru_list);
		list_del_init(&(node->lru_list));
		list_del_init(&(node->hash_list));

	}else{
		node = (lru_node_t*)calloc(1, sizeof(lru_node_t));
		++lru->num_nodes;
		INIT_LIST_HEAD(&node->hash_list);
		INIT_LIST_HEAD(&node->lru_list);
	}

	return node;
}

static lru_node_t* find_lru_node(inode_lru_t* lru, const void* key, size_t key_len)
{
	size_t hash = hashname(key, key_len);
	size_t idx = hash%(lru->options.num_slot);
	struct list_head* head = lru->hash_slots + idx;
	struct list_head* p = NULL;
	list_for_each(p, head){
		lru_node_t* node = list_entry(p, lru_node_t, hash_list);
		if(strcmp(key, node->key) == 0){
			return node;
		}
	}

	return NULL;
}

int insert_lru_node(inode_lru_t* lru, const void* key, size_t key_len, unsigned long ino)
{
	if(!lru || !key || !key_len || ino==0){
		return -1;
	}

	struct lru_node_t* node = find_lru_node(lru, key, key_len);

	if(node){
		list_del(&(node->lru_list));
		if(node->ino != ino){
			node->ino = ino;
		}
		list_add_tail(&(node->lru_list),&lru->lru_nodes);
		return 0;
	}

	node = new_or_expire_lru_node(lru, key, key_len)
	if(node->key){
		free(node->key);
	}
	node->key = calloc(1, key_len);
	node->key_len = key_len;
	memcpy(node->key, key, key_len);
	node->ino = ino;

	list_add_tail(&(node->lru_list), &(lru->lru_nodes));

	size_t hash = hashname(key, key_len);
	size_t idx = hash%(lru->options.num_slot);
	struct list_head* p = lru->hash_slots + idx;
	list_add_tail(&(node->hash_list), p);
	return 0;
}

int del_lru_node(inode_lru_t* lru, const void* key, size_t key_len)
{
	if(!lru || !key || !key_len){
		return 0;
	}

	struct lru_node_t* node = find_lru_node(lru, key, key_len);
	if(!node){
		return 0;
	}

	list_del(&node->hash_list);
	list_del(&node->lru_list);
	list_add_tail(&node->lru_list, &lru->lru_nodes);
	return 0;
}

/*
int util_copy_lru_node_data(lru_t* lru, const void* key, size_t key_len, unsigned long ino, int* expired)
{
	if(!lru || !key || !key_len){
		return 0;
	}

	lru_node_t* node = find_lru_node(lru, key, key_len);
	if(!node){
		return -1;
	}

	if(expired){
		*expired = 0;
	}

	time_t now;
	time(&now);
	if(now >= node->ts + lru->options.expire){
		if(expired) *expired = 1;
	}else{
		//update lru order
		list_del(&node->lru_list);
		list_add_tail(&node->lru_list, &lru->lru_nodes);
		//node->ts = now;
	}

	node->ino=ino;
	return 0;
}
*/