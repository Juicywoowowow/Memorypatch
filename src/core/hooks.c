#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "tracker.h"
#include "memcheck.h"

// Red Zone configuration
// Must be 16 bytes to maintain alignment for SSE/AVX
#define RED_ZONE_SIZE 16
// We use 8-byte magic repeated twice
#define MAGIC_BYTE 0xDEADBEEFCAFEBABE

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

// Helper to write red zones
static void write_red_zones(void* real_ptr, size_t user_size) {
    uint64_t magic = MAGIC_BYTE;
    // Header: Write magic twice to fill 16 bytes
    *(uint64_t*)real_ptr = magic;
    *(uint64_t*)((char*)real_ptr + 8) = magic;

    // Footer: Write magic twice (or once is fine, but lets fill up if we want)
    // Actually we only check 8 bytes in ASM. But let's be consistent.
    // The footer starts at user_ptr + user_size.
    // If we allocate size + 2*RED_ZONE_SIZE, the footer space is 16 bytes.
    void* footer = (char*)real_ptr + RED_ZONE_SIZE + user_size;
    *(uint64_t*)footer = magic;
    *(uint64_t*)((char*)footer + 8) = magic;
}

// Helper to verify red zones
static void verify_red_zones(void* user_ptr, size_t user_size) {
    void* real_ptr = (char*)user_ptr - RED_ZONE_SIZE;
    uint64_t magic = MAGIC_BYTE;

    // Verify first 8 bytes of header
    if (!mp_check_redzone(real_ptr, magic)) {
        fprintf(stderr, "MemoryPatch Error: Header corruption detected at %p!\n", user_ptr);
    }
    // Verify second 8 bytes of header? Optional, but safer.
    if (!mp_check_redzone((char*)real_ptr + 8, magic)) {
        fprintf(stderr, "MemoryPatch Error: Header (part 2) corruption detected at %p!\n", user_ptr);
    }

    // Verify footer
    void* footer_ptr = (char*)user_ptr + user_size;
    if (!mp_check_redzone(footer_ptr, magic)) {
        fprintf(stderr, "MemoryPatch Error: Footer corruption detected at %p (size %zu)!\n", user_ptr, user_size);
    }
}

void* malloc(size_t size) {
    if (in_hook) {
        return bootstrap_alloc(size);
    }

    ensure_init();

    in_hook = 1;

    // Allocate extra for Red Zones
    size_t real_size = size + 2 * RED_ZONE_SIZE;
    void* ptr = real_malloc(real_size);

    if (ptr) {
        write_red_zones(ptr, size);
        void* user_ptr = (char*)ptr + RED_ZONE_SIZE;
        mp_track_alloc(user_ptr, size);
        in_hook = 0;
        return user_ptr;
    }

    in_hook = 0;
    return NULL;
}

void free(void* ptr) {
    if (!ptr) return;

    if (is_bootstrap_ptr(ptr)) {
        return;
    }

    if (in_hook) {
        if (!real_free) real_free = dlsym(RTLD_NEXT, "free");
        if (real_free) real_free(ptr);
        return;
    }

    ensure_init();

    in_hook = 1;

    // Check for corruption before freeing
    // We need the size to check the footer.
    // mp_track_free returns the size of the removed allocation, or 0 if not found.
    // We need to modify mp_track_free to return size, or lookup first.
    // Ideally, mp_track_free should do the lookup.
    // BUT, mp_track_free is in tracker.c.
    // Let's modify mp_track_free to return the Allocation* (detached), so we can check it here, then free the node.
    // OR, just verify inside mp_track_free?
    // Verification needs 'mp_check_redzone' which is ASM. tracker.c can call it.
    // Let's delegate verification to tracker.c? No, tracker is high level.
    // Let's have mp_track_free return the size.

    size_t size = mp_get_allocation_size(ptr); // We need to add this function

    if (size > 0) {
        verify_red_zones(ptr, size);
        mp_track_free(ptr); // This removes it

        // Free real pointer
        real_free((char*)ptr - RED_ZONE_SIZE);
    } else {
        // Not tracked? Might be a wild pointer or something we missed.
        // Or it was allocated before we started?
        // Safest is to just pass it to free?
        // But if we padded it, we must unpad it. If we didn't pad it (e.g. allocated by something else?), passing (ptr-8) is bad.
        // Assumption: All allocations go through us. If it's not in tracker, it might be bad.
        // But realloc might have failed and returned original ptr?
        // Let's warn?
        // fprintf(stderr, "MemoryPatch Warning: Freeing untracked pointer %p\n", ptr);
        real_free(ptr); // Hope for best?
        // If we padded everything, then `ptr` is offset by 8. `free(ptr)` will crash if it was padded.
        // If it wasn't padded, `free(ptr)` is correct.
        // If it's not in tracker, we assume it wasn't padded (or we lost track).
    }

    in_hook = 0;
}

void* calloc(size_t nmemb, size_t size) {
    if (in_hook) {
        return bootstrap_alloc(nmemb * size);
    }

    ensure_init();

    in_hook = 1;

    size_t total_size = nmemb * size;
    size_t real_size = total_size + 2 * RED_ZONE_SIZE;

    // We can't use real_calloc easily because it zeroes everything including our headers.
    // And if we use real_malloc + memset, it's safer for headers.

    void* ptr = real_malloc(real_size);
    if (ptr) {
        // Zero user area
        void* user_ptr = (char*)ptr + RED_ZONE_SIZE;
        memset(user_ptr, 0, total_size);

        write_red_zones(ptr, total_size);
        mp_track_alloc(user_ptr, total_size);
        in_hook = 0;
        return user_ptr;
    }

    in_hook = 0;
    return NULL;
}

void* realloc(void* ptr, size_t size) {
    if (in_hook) {
        if (is_bootstrap_ptr(ptr)) return NULL;
        if (!real_realloc) real_realloc = dlsym(RTLD_NEXT, "realloc");
        return real_realloc(ptr, size);
    }

    if (size == 0 && ptr != NULL) {
        free(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        return malloc(size);
    }

    ensure_init();
    in_hook = 1;

    // We need to realloc the REAL block.
    // And we need to preserve the user data.
    // And we need to check old guards.

    size_t old_size = mp_get_allocation_size(ptr);
    if (old_size > 0) {
        verify_red_zones(ptr, old_size);

        // Untrack old
        mp_track_free(ptr);

        // Realloc real pointer
        void* real_ptr = (char*)ptr - RED_ZONE_SIZE;
        size_t new_real_size = size + 2 * RED_ZONE_SIZE;

        void* new_real_ptr = real_realloc(real_ptr, new_real_size);

        if (new_real_ptr) {
            // Write new guards
            // Note: real_realloc preserves data at start. Header is preserved.
            // Footer needs to be written at new end.
            write_red_zones(new_real_ptr, size);

            void* new_user_ptr = (char*)new_real_ptr + RED_ZONE_SIZE;
            mp_track_alloc(new_user_ptr, size);

            in_hook = 0;
            return new_user_ptr;
        } else {
            // Realloc failed. Original block is valid.
            // But we untracked it! We must re-track it.
            mp_track_alloc(ptr, old_size);
            in_hook = 0;
            return NULL;
        }
    } else {
        // Untracked pointer passed to realloc.
        // Pass through to real_realloc?
        // If we treat it as unpadded, we pass ptr directly.
        void* ret = real_realloc(ptr, size);
        in_hook = 0;
        return ret;
    }
}
