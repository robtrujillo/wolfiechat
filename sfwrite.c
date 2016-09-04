
#include "sfwrite.h"

/**
 * Uses a mutex to lock an output stream so it is not interleaved when
 * printed to by different threads.
 * @param lock Mutex used to lock output stream.
 * @param stream Output stream to write to.
 * @param fmt format string used for varargs. 
 */
void sfwrite(pthread_mutex_t *lock, FILE* stream, char *fmt, ...){
	pthread_mutex_lock(lock);
    va_list args;
    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);
    pthread_mutex_unlock(lock);
}