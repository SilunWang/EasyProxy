#include "cache.h"

extern int cache_size;
extern sem_t list_lock;

/**
 * search for a cache block whose uri is the same
 * @param  head: list head
 * @param  uri
 * @return block ptr
 */
struct cache_block* search_cache(struct cache_block* head, char* uri) {
    struct cache_block* ptr = head->next;
    while (ptr) {
        if (strcmp(ptr->uri, uri) == 0) {
            return ptr;
        }
        else
            ptr = ptr->next;
    }
    return NULL;
}

/**
 * init a block and its semaphore
 * @param blk [description]
 */
void init_cache(struct cache_block* blk) {
    blk->size = 0;
    blk->timestamp = clock();
    blk->next = NULL;
    blk->file = NULL;
    blk->reading_cnt = 0;
    Sem_init(&(blk->lock), 0, 1);
    return;
}

// notice: need to acquire the block's lock
void add_reading_cnt(struct cache_block* blk) {
    if (blk) {
        P(&(blk->lock));
        blk->reading_cnt ++;
        V(&(blk->lock));
    }
}

// notice: need to acquire the block's lock
void sub_reading_cnt(struct cache_block* blk) {
    if (blk) {
        P(&(blk->lock));
        blk->reading_cnt --;
        V(&(blk->lock));
    }
}

/**
 * free a cache block to prevent memory leakage
 * notice: need to acquire the block's lock
 */
void free_cache_node(struct cache_block* blk) {
    if (blk) {
        P(&(blk->lock));
        if (blk->file)
            Free(blk->file);
        Free(blk);
        V(&(blk->lock));
    }
}

/**
 * update a block's timestamp
 * first delete it from list
 * then add it to the end of list
 * notice: need to acquire the block's lock
 */
void update_timestamp(struct cache_block* head, struct cache_block* blk) {
    if (blk) {
        clock_t ts = clock();
        delete_cache(head, blk);
        P(&(blk->lock));
        blk->timestamp = ts;
        V(&(blk->lock));
        add_cache(head, blk);
    }
}

/**
 * add a cache block into the end
 * @param head: list head
 * @param blk: the block to be added
 */
void add_cache(struct cache_block* head, struct cache_block* blk) {
    P(&list_lock);
    struct cache_block* pre = head;
    struct cache_block* ptr = head->next;
    while (ptr) {
        ptr = ptr->next;
        pre = pre->next;
    }
    blk->next = pre->next;
    pre->next = blk;
    cache_size += blk->size;
    V(&list_lock);
    return;
}

/**
 * delete a block from the list
 * but do not free it
 * @param head: list head
 * @param blk: the block to be deleted
 */
void delete_cache(struct cache_block* head, struct cache_block* blk) {
    P(&list_lock);
    struct cache_block* ptr = head->next;
    struct cache_block* pre = head;
    while (ptr) {
        if (ptr == blk) {
            pre->next = ptr->next;
            cache_size -= blk->size;
            break;
        }
        else {
            ptr = ptr->next;
            pre = pre->next;
        }
    }
    V(&list_lock);
    return;
}

/**
 * evict a cache block using LRU from the front
 * if size not enough
 * evict another recursively
 * @param head: list head
 * @param size: least size of cache to be evicted
 */
void evict_cache(struct cache_block* head, int size) {
    P(&list_lock);
    struct cache_block* ptr = head->next;
    struct cache_block* pre = head;
    while (ptr) {
        if (ptr->size < size) {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            size -= ptr->size;
            // ensure no thread is reading from ptr
            while (ptr->reading_cnt != 0)
                sleep(0);
            //free_cache_node(ptr);
            ptr = pre->next;
        }
        else {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            // ensure no thread is reading from ptr
            while (ptr->reading_cnt != 0)
                sleep(0);
            //free_cache_node(ptr);
        }
    }
    V(&list_lock);
}
