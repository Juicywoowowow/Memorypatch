#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void overflow_test() {
    printf("Allocating 10 bytes...\n");
    char* p = malloc(10);
    if (!p) return;

    printf("Writing past the end (overflow)...\n");
    // p is 10 bytes. p[10] is the first byte of the footer.
    // Let's corrupt it.
    p[10] = 0x00;

    printf("Freeing corrupted memory (expecting error)...\n");
    free(p);
}

void underflow_test() {
    printf("Allocating 10 bytes...\n");
    char* p = malloc(10);
    if (!p) return;

    printf("Writing before the start (underflow)...\n");
    // p[-1] is the last byte of the header.
    p[-1] = 0x00;

    printf("Freeing corrupted memory (expecting error)...\n");
    free(p);
}

int main() {
    printf("Running overflow test...\n");
    overflow_test();
    printf("Running underflow test...\n");
    underflow_test();
    return 0;
}
