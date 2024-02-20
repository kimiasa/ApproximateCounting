#ifndef _WINDOW_BIT_COUNT_APX_
#define _WINDOW_BIT_COUNT_APX_

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

uint64_t N_MERGES = 0; // keep track of how many bucket merges occur


typedef struct Bucket{
    uint32_t last_seen; 
    struct Bucket* next;
    struct Bucket* prev;
} Bucket;

typedef struct BucketPool {
    Bucket* pool; 
    Bucket* available;
    uint32_t capacity; 
    uint32_t available_count; 
} BucketPool;

typedef struct Group{
    uint32_t count;
    struct Group* next;
    struct Group* prev;
    struct Bucket* head;
    struct Bucket* tail;
} Group;

typedef struct GroupPool {
    Group* pool; 
    Group* available;
    uint32_t capacity; 
    uint32_t available_count; 
} GroupPool;

typedef struct {
    uint32_t now;
    uint32_t k;
    uint32_t wnd_size;
    uint32_t m;
    uint32_t group_count;
    GroupPool* group_pool;
    Group* group_head;
    Group* group_tail;
    BucketPool* bucket_pool;
    Bucket* head;
    Bucket* tail;
} StateApx;


// k = 1/eps
// if eps = 0.01 (relative error 1%) then k = 100
// if eps = 0.001 (relative error 0.1%) the k = 1000
uint64_t wnd_bit_count_apx_new(StateApx* self, uint32_t wnd_size, uint32_t k) {
    assert(wnd_size >= 1);
    assert(k >= 1);
    uint32_t m = 1 + (uint32_t)ceil(log2(((1.0*wnd_size - 1.0)/ k) + 1.0));
    uint32_t bucket_pool_size = (k+1) * m;
    self->now = 0;
    self->k = k;
    self->wnd_size = wnd_size;
    self->m = m;
    self->group_count = 0;

    BucketPool* pool = (BucketPool*)malloc(sizeof(BucketPool));

    pool->capacity = bucket_pool_size;
    pool->available_count = bucket_pool_size;
    pool->pool = (Bucket*)malloc(sizeof(Bucket) * bucket_pool_size);
    pool->available = NULL;

    if (!pool->pool) {
        free(pool);
        return 0;
    }

    for (size_t i = 0; i < bucket_pool_size; i++) {
        pool->pool[i].next = pool->available;
        pool->available = &pool->pool[i];
    }

    self->bucket_pool = pool;
    self->head = NULL;
    self->tail = NULL;

    GroupPool* group_pool = (GroupPool*)malloc(sizeof(GroupPool));

    group_pool->capacity = m;
    group_pool->available_count = m;
    group_pool->pool = (Group*)malloc(sizeof(Group) * m);
    group_pool->available = NULL;

    if (!group_pool->pool) {
        free(group_pool);
        return 0;
    }

    for (size_t i = 0; i < m; i++) {
        group_pool->pool[i].next = group_pool->available;
        group_pool->available = &group_pool->pool[i];
    }

    self->group_pool = group_pool;
    self->group_head = NULL;
    self->group_tail = NULL;

    uint64_t total_bytes_allocated = sizeof(GroupPool)
                                   + m * sizeof(Group) 
                                   + sizeof(BucketPool) 
                                   + bucket_pool_size * sizeof(Bucket); 


    return total_bytes_allocated;
}

void wnd_bit_count_apx_destruct(StateApx* self) {
    free(self->group_pool->pool);
    free(self->group_pool);
    free(self->bucket_pool->pool);
    free(self->bucket_pool);
}

void wnd_bit_count_apx_print(StateApx* self) {
    printf("k: %d\n", self->k);
    printf("wnd_size: %d\n", self->wnd_size);
    printf("m: %d\n", self->m);
    printf("now: %d\n", self->now);
    printf("group_pool->count: %d\n", self->group_pool->available_count);
    printf("group_pool->available: %p\n", (void*)self->group_pool->available);
    printf("group_head: %p\n", (void*)self->group_head);
    printf("group_tail: %p\n", (void*)self->group_tail);

    printf("bucket_pool->count: %d\n", self->bucket_pool->available_count);
    printf("bucket_pool->available: %p\n", (void*)self->bucket_pool->available);
    printf("head: %p\n", (void*)self->head);
    printf("tail: %p\n", (void*)self->tail);
    if (self->group_head != NULL) {
        printf("group_head: %p, count: %d\n", (void*)self->group_head, self->group_head->count);
    }
    if (self->group_tail != NULL) {
        printf("group_tail: %p, count: %d\n", (void*)self->group_tail, self->group_tail->count);
    }
    if (self->head != NULL) {
        printf("head: time: %d\n", self->head->last_seen);
    }
    if (self->tail != NULL) {
        printf("tail: time: %d\n", self->tail->last_seen);
    }
    printf("Number of merges: %llu\n", N_MERGES);
    
}


uint32_t wnd_bit_count_apx_next(StateApx* self, bool item) {
    
    self->now++;
    //wnd_bit_count_apx_print(self);
    if (self->head != NULL) { 
        if (self->now - self->head->last_seen >= self->wnd_size) {
            Bucket* old_head = self->head;
            self->head = old_head->next;
            if (self->head == NULL) { 
                self->tail = NULL;
            }
            old_head->next = NULL;
            old_head->prev = NULL;
            old_head->last_seen = 0;

            old_head->next = self->bucket_pool->available;
            self->bucket_pool->available = old_head;

            self->bucket_pool->available_count++;

            // * needs testing
            if(self->group_head != NULL) {
                self->group_head->head = self->head;
                self->group_head->count--;
                if (self->group_head->count == 0) {
                    self->group_head = self->group_head->next;
                    self->group_pool->available_count++;
                    self->group_pool->available = self->group_head;
                    self->group_head->prev = NULL;
                    self->group_head->head = self->head;
                    self->group_count--;
                }
            }
            
        }
    }
    if (item == 1) {
        if (self->bucket_pool->available_count <= 0) {
            fprintf(stderr, "Error: No buckets left in the pool.\n");
            exit(EXIT_FAILURE);
            // No available bucket
        } else {
            Bucket* new_bucket = self->bucket_pool->available;
            self->bucket_pool->available = new_bucket->next;
            self->bucket_pool->available_count--;

            new_bucket->last_seen = self->now;
            new_bucket->next = NULL;
            new_bucket->prev = self->tail;

            if (self->tail != NULL) {
                self->tail->next = new_bucket;
            } else {
                self->head = new_bucket;
            }
            self->tail = new_bucket;
        }
        if (self->group_tail == NULL) {
            if (self->bucket_pool->available_count <= 0) {
                fprintf(stderr, "Error: No groups left in the pool.\n");
                exit(EXIT_FAILURE);
                // No available group
            } else {
                Group* new_group = self->group_pool->available;
                self->group_pool->available_count--;
                self->group_pool->available = new_group->next;
                new_group->count = 1;
                new_group->head = self->head;
                new_group->tail = self->tail;
                new_group->next = NULL;
                new_group->prev = NULL;

                if (self->group_tail != NULL) {
                    self->group_tail->next = new_group; 
                } 
                self->group_tail = new_group;

                if (self->group_head == NULL) {
                    self->group_head = new_group;
                }
                self->group_count++;
            }
        } else {
            self->group_tail->count++;
            Group* current = self->group_tail;
            while (current != NULL) {
                if (current->count > self->k + 1) {
                    N_MERGES++;
                    Bucket* current_stale_head = current->head;
                    current->head = current_stale_head->next->next;
                    current->head->prev = current_stale_head->next;
                    current->count -= 2;
                    
                    current_stale_head->next = NULL;
                    current_stale_head->prev = NULL;
                    current_stale_head->last_seen = 0;
                    
                    current_stale_head->next = self->bucket_pool->available;
                    self->bucket_pool->available = current_stale_head;
                    self->bucket_pool->available_count++;

                    if(current->prev != NULL) {
                        current->head->prev->prev = current->prev->tail;
                        current->prev->tail->next = current->head->prev;
                        current->prev->tail = current->head->prev;
                        current->prev->count++;
                    } else {
                        if (self->bucket_pool->available_count <= 0) {
                            fprintf(stderr, "Error: No groups left in the pool.\n");
                            exit(EXIT_FAILURE);
                            // No available group
                        } else {
                            Group* new_group = self->group_pool->available;
                            self->group_pool->available_count--;
                            self->group_pool->available = new_group->next;
                            new_group->count = 1;
                            self->head = current->head->prev;
                            new_group->head = self->head;
                            new_group->tail = self->head;
                            new_group->next = current;
                            new_group->prev = NULL;

                            current->prev = new_group;

                            self->group_head = new_group;  

                            self->group_count++;   
                        }
                        break;
                    }      
                } else {
                    break;
                }
                current = current->prev;
            }
        }
    }
    uint32_t count = 0;
    Group* group_counter = self->group_tail;
    if (self->group_count != 0) {
        for(uint32_t i = 0; i < self->group_count-1; i++) {
        if(group_counter != NULL) {
            count += pow(2, i) * group_counter->count;
            group_counter = group_counter->prev;
        }
    }
    }
    if (self->group_head != NULL) {
         count += (pow(2, self->group_count-1) * (self->group_head->count - 1)) + 1;
    }
    return count;
    
}


#endif // _WINDOW_BIT_COUNT_APX_




