/**
 * @file src/challoc.h
 * @brief Header file which contains the declaration of the functions that are used to allocate and free memory.
 * It should be included if you don't want to use the interposing feature.
 */

#ifndef CHALLOC_H
#define CHALLOC_H

#include <stddef.h>

// If the interposing feature is enabled, have the same API as the standard library
#ifdef CHALLOC_INTERPOSING

/**
 * @brief Allocate memory
 * @param size The size of the memory to allocate
 * @return A pointer to the allocated memory
 */
void* malloc(size_t size);

/**
 * @brief Free memory
 * @param ptr The pointer to the memory to free
 */
void free(void* ptr);

/**
 * @brief Allocate memory and set it to zero
 * @param nmemb The number of elements to allocate
 * @param size The size of each element
 * @return A pointer to the allocated memory
 */
void* calloc(size_t nmemb, size_t size);

/**
 * @brief Reallocate memory
 * @param ptr The pointer to the memory to reallocate
 * @param size The new size of the memory
 * @return A pointer to the reallocated memory
 */
void* realloc(void* ptr, size_t size);
#else
/**
 * @brief Allocate memory
 * @param size The size of the memory to allocate
 * @return A pointer to the allocated memory
 */
void* chamalloc(size_t size);

/**
 * @brief Free memory
 * @param ptr The pointer to the memory to free
 */
void chafree(void* ptr);

/**
 * @brief Allocate memory and set it to zero
 * @param nmemb The number of elements to allocate
 * @param size The size of each element
 * @return A pointer to the allocated memory
 */
void* chacalloc(size_t nmemb, size_t size);

/**
 * @brief Reallocate memory
 * @param ptr The pointer to the memory to reallocate
 * @param size The new size of the memory
 * @return A pointer to the reallocated memory
 */
void* charealloc(void* ptr, size_t size);

#endif

#endif // CHALLOC_H