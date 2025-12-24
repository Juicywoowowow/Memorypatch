#include "hashtable.h"
#include <string.h>

#define TABLE_SIZE 4093

static Allocation* table[TABLE_SIZE];

// Simple pointer hash
static size_t hash(void* ptr) {
    return ((size_t)ptr >> 3) % TABLE_SIZE;
}

void ht_init(void) {
    memset(table, 0, sizeof(table));
}

void ht_insert(Allocation* node) {
    if (!node) return;
    size_t idx = hash(node->ptr);
    node->next = table[idx];
    table[idx] = node;
}

Allocation* ht_remove(void* ptr) {
    size_t idx = hash(ptr);
    Allocation* curr = table[idx];
    Allocation* prev = NULL;

    while (curr) {
        if (curr->ptr == ptr) {
            if (prev) {
                prev->next = curr->next;
            } else {
                table[idx] = curr->next;
            }
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

Allocation* ht_find(void* ptr) {
    size_t idx = hash(ptr);
    Allocation* curr = table[idx];
    while (curr) {
        if (curr->ptr == ptr) return curr;
        curr = curr->next;
    }
    return NULL;
}

void ht_iter(void (*callback)(Allocation*)) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        Allocation* curr = table[i];
        while (curr) {
            callback(curr);
            curr = curr->next;
        }
    }
}
