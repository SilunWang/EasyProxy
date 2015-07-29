#include "cache.h"

extern int cache_size;
extern sem_t list_lock;

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

void init_cache(struct cache_block* blk) {
    blk->size = 0;
    blk->timestamp = clock();
    blk->next = NULL;
    blk->file = NULL;
    blk->reading_cnt = 0;
    Sem_init(&(blk->lock), 0, 1);
    return;
}

void add_reading_cnt(struct cache_block* blk) {
    if (blk) {
        P(&(blk->lock));
        blk->reading_cnt ++;
        V(&(blk->lock));
    }
}

void sub_reading_cnt(struct cache_block* blk) {
    if (blk) {
        P(&(blk->lock));
        blk->reading_cnt --;
        V(&(blk->lock));
    }
}

void free_cache_node(struct cache_block* blk) {
    if (blk) {
        P(&(blk->lock));
        if (blk->file)
            Free(blk->file);
        Free(blk);
        V(&(blk->lock));
    }
}

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

void evict_cache(struct cache_block* head, int size) {
    P(&list_lock);
    struct cache_block* ptr = head->next;
    struct cache_block* pre = head;
    while (ptr) {
        if (ptr->size < size) {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            size -= ptr->size;
            while (ptr->reading_cnt != 0)
                sleep(0);
            free_cache_node(ptr);
            ptr = pre->next;
        }
        else {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            while (ptr->reading_cnt != 0)
                sleep(0);
            free_cache_node(ptr);
        }
    }
    V(&list_lock);
}
