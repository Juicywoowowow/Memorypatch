#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdint.h>
#include "unwind.h" // For MAX_STACK_DEPTH logic if needed, but struct is defined here

#define MAX_STACK_DEPTH 16

typedef struct Allocation {
    void* ptr;
    size_t size;
    void* stack[MAX_STACK_DEPTH];
    int stack_count;
    uint64_t timestamp;
    struct Allocation* next; // For chaining
} Allocation;

/*
 * Initialize the hash table.
 */
void ht_init(void);

/*
 * Insert an allocation into the hash table.
 * Thread-safety is NOT handled here (caller must lock).
 */
void ht_insert(Allocation* node);

/*
 * Find and remove an allocation by pointer.
 * Returns the node (caller must free it), or NULL if not found.
 * Thread-safety is NOT handled here.
 */
Allocation* ht_remove(void* ptr);

/*
 * Find an allocation by pointer.
 * Returns the node or NULL.
 */
Allocation* ht_find(void* ptr);

/*
 * Iterate over all items.
 * Callback is called for each item.
 */
void ht_iter(void (*callback)(Allocation*));

#endif
