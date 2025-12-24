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

#define MAX_STACK_DEPTH 16

// Structure to track a single allocation
typedef struct Allocation {
    void* ptr;
    size_t size;
    void* stack[MAX_STACK_DEPTH];
    int stack_count;
    uint64_t timestamp;
    struct Allocation* next;
} Allocation;

static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

static Allocation* head = NULL;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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
    init_real_functions();
    initialized = 1;
    // Register the report generator to run at exit
    atexit(mp_generate_report);
}

void mp_track_alloc(void* ptr, size_t size) {
    if (!ptr) return;
    if (!initialized) mp_tracker_init();

    // Use real_malloc to allocate the node
    Allocation* node = (Allocation*)real_malloc(sizeof(Allocation));
    if (!node) return; // OOM in tracker? Bad luck.

    node->ptr = ptr;
    node->size = size;
    node->timestamp = rdtsc();

    // Capture stack trace
    // We pass buffer and max depth
    node->stack_count = mp_unwind(node->stack, MAX_STACK_DEPTH);

    // Add to list (thread-safe)
    pthread_mutex_lock(&lock);
    node->next = head;
    head = node;
    pthread_mutex_unlock(&lock);
}

void mp_track_free(void* ptr) {
    if (!ptr) return;
    if (!initialized) mp_tracker_init();

    pthread_mutex_lock(&lock);
    Allocation* curr = head;
    Allocation* prev = NULL;

    while (curr) {
        if (curr->ptr == ptr) {
            // Found it, remove from list
            if (prev) {
                prev->next = curr->next;
            } else {
                head = curr->next;
            }
            // Free the node itself using real_free
            real_free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&lock);
}

void mp_generate_report(void) {
    // If we want to support output file configuration, we can read env var or default
    const char* report_file = getenv("MEMORYPATCH_OUTPUT");
    if (!report_file) report_file = "memorypatch_report.txt";

    FILE* fp = fopen(report_file, "w");
    if (!fp) {
        // Try stderr if file fails
        fp = stderr;
        fprintf(fp, "memorypatch: Could not open report file. Printing to stderr.\n");
    }

    fprintf(fp, "=== MemoryPatch Leak Report ===\n");

    pthread_mutex_lock(&lock);
    Allocation* curr = head;
    int leak_count = 0;
    size_t total_leaked = 0;

    while (curr) {
        leak_count++;
        total_leaked += curr->size;

        fprintf(fp, "\nLeak #%d: Address %p, Size %zu bytes, Timestamp %lu\n", leak_count, curr->ptr, curr->size, curr->timestamp);
        fprintf(fp, "Allocation Stack Trace:\n");
        for (int i = 0; i < curr->stack_count; i++) {
            // Skip the first few frames if they are inside memorypatch itself
            // typically frame 0 is mp_unwind, frame 1 is mp_track_alloc, frame 2 is malloc hook
            // but mp_unwind starts at *its* caller, so:
            // 0: mp_track_alloc
            // 1: malloc hook
            // 2: user code
            // This depends on inlining, but let's print everything for now
            fprintf(fp, "  [%d] %p\n", i, curr->stack[i]);
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&lock);

    if (leak_count == 0) {
        fprintf(fp, "\nNo memory leaks detected!\n");
    } else {
        fprintf(fp, "\nTotal Leaks: %d\n", leak_count);
        fprintf(fp, "Total Bytes Leaked: %zu\n", total_leaked);
    }

    if (fp != stderr) fclose(fp);
}
