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
        clock_t ts = clock();
        while (blk->is_writing || blk->is_reading)
            sleep(0);
        writelock_cache(blk);
        delete_cache(head, blk);
        blk->timestamp = ts;
        add_cache(head, blk);
        writeunlock_cache(blk);
    }
}

void readlock_cache(struct cache_block* blk) {
    if (blk) {
        blk->is_reading++;
    }
}

void writelock_cache(struct cache_block* blk) {
    if (blk) {
        blk->is_writing = 1;
    }
}

void readunlock_cache(struct cache_block* blk) {
    if (blk) {
        blk->is_reading--;
    }
}

void writeunlock_cache(struct cache_block* blk) {
    if (blk) {
        blk->is_writing = 0;
    }
}

void add_cache(struct cache_block* head, struct cache_block* blk) {
    struct cache_block* pre = head;
    struct cache_block* ptr = head->next;
    while (ptr) {
        ptr = ptr->next;
        pre = pre->next;
    }
    blk->next = pre->next;
    pre->next = blk;
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
        if (ptr->size < size) {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            free_cache_node(ptr);
            evict_cache(head, size - ptr->size);
        }
        else {
            pre->next = ptr->next;
            cache_size -= ptr->size;
            free_cache_node(ptr);
        }
    }
}