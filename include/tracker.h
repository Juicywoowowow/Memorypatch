#ifndef TRACKER_H
#define TRACKER_H

#include <stddef.h>

/*
 * Initializes the tracker (mutexes, output file path, etc).
 */
void mp_tracker_init(void);

/*
 * Records an allocation.
 * @param ptr The address returned to the user.
 * @param size The size of the allocation.
 */
void mp_track_alloc(void* ptr, size_t size);

/*
 * Records a deallocation.
 * @param ptr The address being freed.
 */
void mp_track_free(void* ptr);

/*
 * Get the size of an allocation.
 * @param ptr The address to look up.
 * @return The size, or 0 if not found.
 */
size_t mp_get_allocation_size(void* ptr);

/*
 * Generates the leak report.
 * Usually called automatically via atexit, but exposed here just in case.
 */
void mp_generate_report(void);

#endif
