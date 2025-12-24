#include <stdlib.h>
#include <stdio.h>

void leak_memory() {
    void* p = malloc(128);
    printf("Allocated 128 bytes at %p, intentionally leaking it.\n", p);
    // No free(p)
}

void clean_memory() {
    void* p = malloc(64);
    printf("Allocated 64 bytes at %p, freeing it.\n", p);
    free(p);
}

int main() {
    printf("Starting leaky app...\n");
    leak_memory();
    clean_memory();
    printf("Exiting leaky app...\n");
    return 0;
}
