#include "csapp.h"

struct cache_block {
    char uri[MAXLINE];
    clock_t timestamp;
    int semaphore;
    int size;
    char* file;
    struct cache_block* next;
};

struct cache_block* search_cache(struct cache_block* head, char* uri);
void update_timestamp(struct cache_block* head, struct cache_block* blk);
void lock_cache(struct cache_block* blk);
void unlock_cache(struct cache_block* blk);
void add_cache(struct cache_block* head, struct cache_block* blk);
void delete_cache(struct cache_block* head, struct cache_block* blk);
void evict_cache(struct cache_block* head, int size);
void init_cache(struct cache_block* blk);
void free_cache(struct cache_block* head);
void free_cache_node(struct cache_block* blk);