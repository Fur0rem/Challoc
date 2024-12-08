#include "challoc.h"
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct {
	size_t size;
} AllocMetadata;

void* chamalloc(size_t size) {
	// printf("chamalloc\n");
	// Allocate memory for the metadata + user data
	size	  = size + sizeof(AllocMetadata);
	void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	// Check for errors
	if (ptr == MAP_FAILED) {
		return NULL;
	}

	// Write the size of the memory block into the metadata
	AllocMetadata* metadata = (AllocMetadata*)ptr;
	metadata->size		= size;

	// Return a pointer to the user data
	return ptr + sizeof(AllocMetadata);
}

void chafree(void* ptr) {
	// Get the metadata
	AllocMetadata* metadata = (AllocMetadata*)(ptr - sizeof(AllocMetadata));

	// Free the memory
	munmap(metadata, metadata->size);
}

void* chacalloc(size_t nmemb, size_t size) {
	// Allocate memory
	void* ptr = chamalloc(nmemb * size);

	// Set memory to zero
	memset(ptr, 0, nmemb * size);

	return ptr;
}

void* charealloc(void* ptr, size_t size) {
	// Allocate new memory
	void* new_ptr = chamalloc(size);

	// Copy the old data without the metadata
	AllocMetadata* metadata = (AllocMetadata*)(ptr - sizeof(AllocMetadata));
	memcpy(new_ptr, ptr, metadata->size - sizeof(AllocMetadata));

	// Free the old memory
	chafree(ptr);

	return new_ptr;
}

#ifdef CHALLOC_INTERPOSING
void* malloc(size_t size) {
	return chamalloc(size);
}

void free(void* ptr) {
	chafree(ptr);
}

void* calloc(size_t nmemb, size_t size) {
	return chacalloc(nmemb, size);
}

void* realloc(void* ptr, size_t size) {
	return charealloc(ptr, size);
}
#endif