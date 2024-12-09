#include "challoc.h"
#include <execinfo.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
	size_t size;
} AllocMetadata;

AllocMetadata* challoc_get_metadata(void* ptr) {
	return (AllocMetadata*)(ptr - sizeof(AllocMetadata));
}

void* __chamalloc(size_t size) {
	// Allocate memory for the metadata + user data
	size	  = size + sizeof(AllocMetadata);
	void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	// Check for errors
	if (ptr == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	// Write the size of the memory block into the metadata
	AllocMetadata* metadata = (AllocMetadata*)ptr;
	metadata->size		= size;

	// Return a pointer to the user data
	return ptr + sizeof(AllocMetadata);
}

void __chafree(void* ptr) {
	if (ptr == NULL) {
		return;
	}

	// Get the metadata
	AllocMetadata* metadata = (AllocMetadata*)(ptr - sizeof(AllocMetadata));

	// Free the memory
	if (munmap(metadata, metadata->size) == -1) {
		perror("munmap");
	}
}

void* __chacalloc(size_t nmemb, size_t size) {
	// Allocate memory
	void* ptr = __chamalloc(nmemb * size);

	// Set memory to zero
	if (ptr != NULL) {
		memset(ptr, 0, nmemb * size);
	}

	return ptr;
}

void* __charealloc(void* ptr, size_t size) {
	// Allocate new memory
	void* new_ptr = __chamalloc(size);

	// Copy the old data without the metadata
	if (new_ptr != NULL && ptr != NULL) {
		AllocMetadata* metadata = challoc_get_metadata(ptr);
		memcpy(new_ptr, ptr, metadata->size - sizeof(AllocMetadata));
	}

	// Free the old memory
	__chafree(ptr);

	return new_ptr;
}

#ifdef CHALLOC_LEAKCHECK

void* __chamalloc_untracked(size_t size) {
	return __chamalloc(size);
}

void __chafree_untracked(void* ptr) {
	__chafree(ptr);
}

typedef struct {
	void* ptr;
	size_t size;
} AllocTrace;

void AllocTrace_free(AllocTrace trace) {}

typedef struct {
	AllocTrace* ptrs;
	size_t size;
	size_t capacity;
} AllocList;

AllocList AllocList_with_capacity(size_t capacity) {
	return (AllocList){
	    .ptrs     = __chamalloc_untracked(capacity * sizeof(AllocTrace)),
	    .size     = 0,
	    .capacity = capacity,
	};
}

void AllocList_push(AllocList* list, void* ptr, size_t ptr_size) {
	if (list->size == list->capacity) {
		list->capacity *= 2;
		AllocTrace* new_ptrs = __chamalloc_untracked(list->capacity * sizeof(AllocTrace));
		memcpy(new_ptrs, list->ptrs, list->size * sizeof(AllocTrace));
		__chafree_untracked(list->ptrs);
		list->ptrs = new_ptrs;
	}
	list->ptrs[list->size] = (AllocTrace){
	    .ptr  = ptr,
	    .size = ptr_size,
	};
	list->size++;
}

void AllocList_remove_ptr(AllocList* list, void* ptr) {
	for (size_t i = 0; i < list->size; i++) {
		if (list->ptrs[i].ptr == ptr) {
			list->ptrs[i] = list->ptrs[list->size - 1];
			list->size--;
			AllocTrace_free(list->ptrs[list->size]);
			return;
		}
	}
}

void AllocList_free(AllocList* list) {
	for (size_t i = 0; i < list->size; i++) {
		AllocTrace_free(list->ptrs[i]);
	}
	__chafree_untracked(list->ptrs);
}

AllocList alloc_list = {0};

void __attribute__((constructor)) init() {
	printf("challoc: checking for memory leaks\n");
	alloc_list = AllocList_with_capacity(10);
}

void __attribute__((destructor)) fini() {
	if (alloc_list.size > 0) {
		fprintf(stderr, "challoc: detected %zu memory leaks\n", alloc_list.size);
		for (size_t i = 0; i < alloc_list.size; i++) {
			fprintf(stderr, "Leaked memory at %p, Content: ", alloc_list.ptrs[i].ptr);
			size_t ptr_size = alloc_list.ptrs[i].size;
			for (size_t j = 0; j < ptr_size; j++) {
				fprintf(stderr, "%02X ", ((uint8_t*)alloc_list.ptrs[i].ptr)[j]);
			}
			fprintf(stderr, "\n");
		}
		exit(1);
	}
	printf("challoc: no memory leaks detected, congrats!\n");
	printf(" /\\_/\\\n>(^w^)<\n  b d\n");
	AllocList_free(&alloc_list);
}

#endif

void* chamalloc(size_t size) {
	void* ptr = __chamalloc(size);
#ifdef CHALLOC_LEAKCHECK
	AllocList_push(&alloc_list, ptr, size);
#endif
	return ptr;
}

void chafree(void* ptr) {
#ifdef CHALLOC_LEAKCHECK
	AllocList_remove_ptr(&alloc_list, ptr);
#endif
	__chafree(ptr);
}

void* chacalloc(size_t nmemb, size_t size) {
	void* ptr = __chacalloc(nmemb, size);
#ifdef CHALLOC_LEAKCHECK
	AllocList_push(&alloc_list, ptr, nmemb * size);
#endif
	return ptr;
}

void* charealloc(void* ptr, size_t size) {
	void* new_ptr = __charealloc(ptr, size);
#ifdef CHALLOC_LEAKCHECK
	AllocList_remove_ptr(&alloc_list, ptr);
	AllocList_push(&alloc_list, new_ptr, size);
#endif
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