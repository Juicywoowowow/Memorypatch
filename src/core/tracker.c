#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include "tracker.h"
#include "unwind.h"
#include "hashtable.h"

// Note: Allocation struct is now defined in hashtable.h

static pthread_mutex_t lock;
static int lock_initialized = 0;

static void ensure_lock() {
    if (!lock_initialized) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&lock, &attr);
        pthread_mutexattr_destroy(&attr);
        lock_initialized = 1;
    }
}

// We need to resolve the real malloc/free to allocate our own nodes without recursion
static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void*) = NULL;

static int initialized = 0;

static void init_real_functions() {
    if (!real_malloc) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
        if (!real_malloc) {
            fprintf(stderr, "Error: Could not resolve real malloc\n");
            exit(1);
        }
    }
    if (!real_free) {
        real_free = dlsym(RTLD_NEXT, "free");
        if (!real_free) {
            fprintf(stderr, "Error: Could not resolve real free\n");
            exit(1);
        }
    }
}

void mp_tracker_init(void) {
    if (initialized) return;
    ensure_lock();
    init_real_functions();
    ht_init();
    initialized = 1;
    // Register the report generator to run at exit
    atexit(mp_generate_report);
}

static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void mp_track_alloc(void* ptr, size_t size) {
    if (!ptr) return;
    if (!initialized) mp_tracker_init();

    // Use real_malloc to allocate the node
    Allocation* node = (Allocation*)real_malloc(sizeof(Allocation));
    if (!node) return; // OOM in tracker

    node->ptr = ptr;
    node->size = size;
    node->timestamp = rdtsc();
    node->stack_count = mp_unwind(node->stack, MAX_STACK_DEPTH);

    ensure_lock();
    pthread_mutex_lock(&lock);
    ht_insert(node);
    pthread_mutex_unlock(&lock);
}

void mp_track_free(void* ptr) {
    if (!ptr) return;
    if (!initialized) mp_tracker_init();

    ensure_lock();
    pthread_mutex_lock(&lock);
    Allocation* node = ht_remove(ptr);
    pthread_mutex_unlock(&lock);

    if (node) {
        real_free(node);
    }
}

size_t mp_get_allocation_size(void* ptr) {
    if (!ptr) return 0;
    if (!initialized) return 0;

    ensure_lock();
    pthread_mutex_lock(&lock);
    Allocation* node = ht_find(ptr);
    size_t size = node ? node->size : 0;
    pthread_mutex_unlock(&lock);

    return size;
}

void mp_generate_report(void) {
    const char* report_file = getenv("MEMORYPATCH_OUTPUT");
    if (!report_file) report_file = "memorypatch_report.txt";

    FILE* fp = fopen(report_file, "w");
    if (!fp) {
        fp = stderr;
        fprintf(fp, "memorypatch: Could not open report file. Printing to stderr.\n");
    }

    fprintf(fp, "=== MemoryPatch Leak Report ===\n");

    ensure_lock();
    pthread_mutex_lock(&lock);

    // Iteration helper context
    struct ReportCtx {
        FILE* fp;
        int count;
        size_t bytes;
    } ctx = { fp, 0, 0 };

    void callback(Allocation* curr) {
        ctx.count++;
        ctx.bytes += curr->size;

        fprintf(ctx.fp, "\nLeak #%d: Address %p, Size %zu bytes, Timestamp %lu\n", ctx.count, curr->ptr, curr->size, curr->timestamp);
        fprintf(ctx.fp, "Allocation Stack Trace:\n");
        for (int i = 0; i < curr->stack_count; i++) {
            Dl_info info;
            void* addr = curr->stack[i];
            if (dladdr(addr, &info) && info.dli_sname) {
                 fprintf(ctx.fp, "  [%d] %p < %s + %td >\n", i, addr, info.dli_sname, (char*)addr - (char*)info.dli_saddr);
            } else {
                 fprintf(ctx.fp, "  [%d] %p\n", i, addr);
            }
        }
    }

    ht_iter(callback);

    pthread_mutex_unlock(&lock);

    if (ctx.count == 0) {
        fprintf(fp, "\nNo memory leaks detected!\n");
    } else {
        fprintf(fp, "\nTotal Leaks: %d\n", ctx.count);
        fprintf(fp, "Total Bytes Leaked: %zu\n", ctx.bytes);
    }

    if (fp != stderr) fclose(fp);
}
