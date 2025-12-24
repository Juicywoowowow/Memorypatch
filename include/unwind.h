#ifndef MP_UNWIND_H
#define MP_UNWIND_H

/*
 * Captures the current stack trace.
 * @param buffer Array to store return addresses.
 * @param max_depth Maximum size of the buffer.
 * @return Number of frames captured.
 */
int mp_unwind(void** buffer, int max_depth);

#endif
