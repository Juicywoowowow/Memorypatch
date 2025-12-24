#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include "tracker.h"

// Pointers to real functions
static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void*) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;

// Recursion protection
static __thread int in_hook = 0;

// Static buffer for bootstrap allocations (dlsym calls calloc/malloc)
static char bootstrap_buf[4096];
static size_t bootstrap_offset = 0;

static void* bootstrap_alloc(size_t size) {
    // 8-byte alignment
    size = (size + 7) & ~7;

    if (bootstrap_offset + size > sizeof(bootstrap_buf)) {
        // We are out of bootstrap memory. This is fatal during dlsym.
        return NULL;
    }
    void* ptr = bootstrap_buf + bootstrap_offset;
    bootstrap_offset += size;
    return ptr;
}

static int is_bootstrap_ptr(void* ptr) {
    return (char*)ptr >= bootstrap_buf && (char*)ptr < (bootstrap_buf + sizeof(bootstrap_buf));
}

static void ensure_init() {
    if (!real_malloc) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
        real_free = dlsym(RTLD_NEXT, "free");
        real_calloc = dlsym(RTLD_NEXT, "calloc");
        real_realloc = dlsym(RTLD_NEXT, "realloc");
        mp_tracker_init();
    }
}

void* malloc(size_t size) {
    if (in_hook) {
        return bootstrap_alloc(size);
    }

    ensure_init();

    in_hook = 1;
    void* ptr = real_malloc(size);
    if (ptr) {
        mp_track_alloc(ptr, size);
    }
    in_hook = 0;

    return ptr;
}

void free(void* ptr) {
    if (!ptr) return;

    if (is_bootstrap_ptr(ptr)) {
        return;
    }

    if (in_hook) {
        // This case is weird: we are freeing something while inside a hook?
        // Maybe dlsym failed and tries to free?
        // We try to find real_free if possible, otherwise leak (safe).
        if (!real_free) real_free = dlsym(RTLD_NEXT, "free");
        if (real_free) real_free(ptr);
        return;
    }

    ensure_init();

    in_hook = 1;
    mp_track_free(ptr);
    real_free(ptr);
    in_hook = 0;
}

void* calloc(size_t nmemb, size_t size) {
    if (in_hook) {
        return bootstrap_alloc(nmemb * size);
    }

    ensure_init();

    in_hook = 1;
    void* ptr = real_calloc(nmemb, size);
    if (ptr) {
        mp_track_alloc(ptr, nmemb * size);
    }
    in_hook = 0;
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (in_hook) {
        if (is_bootstrap_ptr(ptr)) {
             // Reallocating a bootstrap pointer.
             // If size fits in remaining bootstrap buffer, maybe?
             // But we can't easily expand in place.
             // Best effort: allocate new bootstrap, copy (if we knew old size...), return.
             // We don't know old size. This is dangerous.
             // Return NULL (fail) is safest for bootstrap edge cases.
             return NULL;
        }
        if (!real_realloc) real_realloc = dlsym(RTLD_NEXT, "realloc");
        return real_realloc(ptr, size);
    }

    // Handle realloc(ptr, 0) -> free(ptr) behavior
    if (size == 0 && ptr != NULL) {
        free(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        return malloc(size);
    }

    ensure_init();

    in_hook = 1;

    // We need to handle tracking carefully.
    // If real_realloc moves the block, the old pointer is invalid.
    // If real_realloc fails, the old pointer is valid.

    void* new_ptr = real_realloc(ptr, size);

    if (new_ptr) {
        // Success. Untrack old, track new.
        // Note: mp_track_free will find the node by 'ptr'.
        // If 'new_ptr' == 'ptr', we still need to update the size.
        // mp_track_free removes the node completely. mp_track_alloc adds new.
        // This handles both in-place resize and move.
        mp_track_free(ptr);
        mp_track_alloc(new_ptr, size);
    }
    // If new_ptr is NULL, realloc failed. ptr is still valid and tracked. No change.

    in_hook = 0;
    return new_ptr;
}
