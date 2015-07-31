#include "csapp.h"

struct cache_block {
    char uri[MAXLINE];
    clock_t timestamp;
    // this lock protects vars: reading_cnt, timestamp
    sem_t lock;
    int reading_cnt;
    int size;
    char* file;
    struct cache_block* next;
};

struct cache_block* search_cache(struct cache_block* head, char* uri);
void update_timestamp(struct cache_block* head, struct cache_block* blk);
void add_cache(struct cache_block* head, struct cache_block* blk);
void delete_cache(struct cache_block* head, struct cache_block* blk);
void evict_cache(struct cache_block* head, int size);
void init_cache(struct cache_block* blk);
void free_cache_node(struct cache_block* blk);
void add_reading_cnt(struct cache_block* blk);
void sub_reading_cnt(struct cache_block* blk);