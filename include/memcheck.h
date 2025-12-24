#ifndef MEMCHECK_H
#define MEMCHECK_H

#include <stdint.h>

/*
 * Checks if the memory at ptr matches the magic value.
 * Implemented in Assembly.
 * @return 1 if match, 0 if mismatch.
 */
int mp_check_redzone(void* ptr, uint64_t magic);

#endif
