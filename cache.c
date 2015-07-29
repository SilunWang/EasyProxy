#include "cache.h"

extern int cache_size;

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
    blk = (struct cache_block*) malloc(sizeof(struct cache_block));
    blk->size = 0;
    blk->timestamp = clock();
    blk->semaphore = 1;
    blk->next = NULL;
    blk->file = NULL;
    return;
}

void free_cache(struct cache_block* head) {
    if (head)
        free_cache(head->next);
    free_cache_node(head);
}

void free_cache_node(struct cache_block* blk) {
    if (blk) {
        if (blk->file)
            Free(blk->file);
        Free(blk);
    }
}

void update_timestamp(struct cache_block* head, struct cache_block* blk) {
    if (blk) {
        while (blk->semaphore == 0)
            sleep(0);
        lock_cache(blk);
        delete_cache(head, blk);
        blk->timestamp = clock();
        add_cache(head, blk);
        unlock_cache(blk);
    }
}

void lock_cache(struct cache_block* blk) {
    if (blk) {
        blk->semaphore = 0;
    }
}

void unlock_cache(struct cache_block* blk) {
    if (blk) {
        blk->semaphore = 1;
    }
}

void add_cache(struct cache_block* head, struct cache_block* blk) {
    blk->next = head->next;
    head->next = blk;
    cache_size += blk->size;
    return;
}

void delete_cache(struct cache_block* head, struct cache_block* blk) {
    struct cache_block* ptr = head->next;
    struct cache_block* pre = head;
    while (ptr) {
        if (ptr == blk) {
            pre->next = ptr->next;
            cache_size -= blk->size;
            return;
        }
        else {
            ptr = ptr->next;
            pre = pre->next;
        }
    }
}

void evict_cache(struct cache_block* head, int size) {
    struct cache_block* ptr = head->next;
    struct cache_block* pre = head;
    while (ptr) {
        lock_cache(ptr);
        if (ptr->size < size) {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            unlock_cache(ptr);
            free_cache_node(ptr);
            evict_cache(head, size - ptr->size);
        }
        else {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            unlock_cache(ptr);
            free_cache_node(ptr);
        }
    }
}