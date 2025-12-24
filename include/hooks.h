#ifndef HOOKS_H
#define HOOKS_H

#include <stddef.h>

/*
 * Standard malloc/free signatures
 */
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

#endif
