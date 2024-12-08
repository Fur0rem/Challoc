#ifndef CHALLOC_H
#define CHALLOC_H

#include <stddef.h>

#ifdef CHALLOC_INTERPOSING
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
#else
void* chamalloc(size_t size);
void chafree(void* ptr);
void* chacalloc(size_t nmemb, size_t size);
void* charealloc(void* ptr, size_t size);
#endif

#endif // CHALLOC_H