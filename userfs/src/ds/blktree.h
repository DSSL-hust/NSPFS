#include "rbtree.h"

static int fileoffcmp(unsigned long offset, unsigned long fileoff, unsigned long size){
	if(offset < fileoff)
		return -1;
	if(offset >= (fileoff+size))
		return 1;
	return 0;

}

static int nodecmp(unsigned long offset, unsigned long fileoff, unsigned long size){
    if(offset < fileoff)
        return -1;
    if(offset > (fileoff+size))
        return 1;
    return 0;

}


static struct block_node * block_search(struct rb_root *root, unsigned long offset){
	
	struct rb_node *node = root->rb_node;

    while (node) {
        struct block_node *block = container_of(node, struct block_node, node);
       	int result;
        result = fileoffcmp(offset, block->fileoff, block->size);
        if (result < 0)
              node = node->rb_left;
        else if (result > 0)
              node = node->rb_right;
        else
              return block;
    }
    return NULL;
}



static int block_insert(struct rb_root *root, struct block_node *block)
{
    struct rb_node **new = &(root->rb_node), *parent = NULL;

   /* Figure out where to put new node */
    while (*new) {
        struct block_node *this = container_of(*new, struct block_node, node);
        //int result = strcmp(data->string, this->string);
        parent = *new;
        
        int result;
        result = nodecmp(block->fileoff, this->fileoff, this->size);
        if (result < 0)
            new = &((*new)->rb_left);
        else if (result > 0)
             new = &((*new)->rb_right);
        else{
            if(this->devid == block->devid && ((block->addr-this->addr)<<12)== this->size ){
                this->size = block->fileoff  + block->size -this->fileoff;
                return 0;
            }else{
                break;
            }
        }
        
       
    }
    
    struct rb_node **newnode = &(root->rb_node);
    parent=NULL;
    while (*newnode) {
        struct block_node *this = container_of(*newnode, struct block_node, node);
        parent = *newnode;
        if (block->fileoff < this->fileoff)
            newnode = &((*newnode)->rb_left);
        else if (block->fileoff > this->fileoff)
            newnode = &((*newnode)->rb_right);
        else
            return 0;
    }
    /* Add new node and rebalance tree. */
    rb_link_node(&block->node, parent, newnode);
    rb_insert_color(&block->node, root);

    return 1;
}


static void block_clear(struct rb_root *root) {
    userfs_assert(root);
    struct rb_node *newnode = NULL;

    struct block_node *curr = NULL;

    for (newnode = rb_first(root); newnode != NULL; newnode = rb_next(newnode)) {
        curr = rb_entry(newnode, struct block_node, node);
        rb_erase(&(curr->node), root);
    }
}

static void block_replace(struct rb_root *root, struct block_node *block, unsigned long offset) {
    
    struct block_node *pnode = block_search(root, offset);
    if (pnode)
        rb_replace_node(&(pnode->node), &(block->node), root);
}


/* 删除一个节点 */
static void block_delete (struct rb_root *root, unsigned long offset) {

    struct block_node *node = block_search(root, offset);
    if(node)
        rb_erase(&(node->node), root);
}
