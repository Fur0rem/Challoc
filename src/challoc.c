#include "challoc.h"
#include <assert.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

pthread_mutex_t challoc_mutex	 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t alloc_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Slab allocation for small memory blocks
typedef struct {
	uint8_t slab512_usage : 1;
	uint8_t slab256_usage : 2;
	uint8_t slab128_usage : 4;
} Slab512x256x128Usage;

__attribute__((aligned(4096))) typedef struct {
	uint8_t slab512[1][512];
	uint8_t slab256[2][256];
	uint8_t slab128[4][128];
	uint8_t slab64[8][64];
	uint8_t slab32[16][32];
	uint8_t slab16[32][16];
	uint8_t slab8[64][8];
	uint8_t slab_small[64][4];

	uint64_t slab_small_usage;
	uint64_t slab8_usage;
	uint32_t slab16_usage;
	uint16_t slab32_usage;
	uint8_t slab64_usage;
	Slab512x256x128Usage slab512x256x128_usage;
} MiniSlab;

MiniSlab challoc_minislab = {0};

typedef struct {
	bool is_close : 1;
	uint8_t ceil_pow2 : 5; // 512 is the max power of two we can reach, 2^9 = 512, ceil(log2(512)) = 9
} ClosePowerOfTwo;

ClosePowerOfTwo is_close_to_power_of_two(size_t size) {
	ClosePowerOfTwo false_result = {
	    .is_close  = false,
	    .ceil_pow2 = 0,
	};

	if (size == 0 || size > 512) {
		return false_result;
	}

	if (size <= 4) {
		return (ClosePowerOfTwo){
		    .is_close  = true,
		    .ceil_pow2 = 2,
		};
	}

	size_t ceil_pow2 = 2 * 2;
	size_t pow2	 = 2;
	while (ceil_pow2 < size) {
		ceil_pow2 *= 2;
		pow2++;
	}

	if ((double)ceil_pow2 / (double)size <= 1.2) {
		return (ClosePowerOfTwo){
		    .is_close  = true,
		    .ceil_pow2 = pow2,
		};
	}

	return false_result;
}

size_t find_first_bit_at_0(uint64_t value) {
	// go from the left to the right
	for (size_t i = 0; i < 64; i++) {
		if ((value & (1ULL << i)) == 0) {
			return i;
		}
	}
	return 0;
}

void* minislab_alloc(ClosePowerOfTwo size) {
	assert(size.is_close);
	size_t ceil = size.ceil_pow2;
	assert(ceil >= 2 && ceil <= 9);
	switch (size.ceil_pow2) {
		case 2: {
			if (challoc_minislab.slab_small_usage == 0xFFFFFFFF) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab_small_usage);
			challoc_minislab.slab_small_usage |= 1ULL << index;
			return challoc_minislab.slab_small[index];
		}
		case 3: {
			if (challoc_minislab.slab8_usage == 0xFFFFFFFF) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab8_usage);
			challoc_minislab.slab8_usage |= 1ULL << index;
			return challoc_minislab.slab8[index];
		}
		case 4: {
			if (challoc_minislab.slab16_usage == 0xFFFF) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab16_usage);
			challoc_minislab.slab16_usage |= 1UL << index;
			return challoc_minislab.slab16[index];
		}
		case 5: {
			if (challoc_minislab.slab32_usage == 0xFF) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab32_usage);
			challoc_minislab.slab32_usage |= 1U << index;
			return challoc_minislab.slab32[index];
		}
		case 6: {
			if (challoc_minislab.slab64_usage == 0xF) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab64_usage);
			challoc_minislab.slab64_usage |= 1U << index;
			return challoc_minislab.slab64[index];
		}
		case 7: {
			if (challoc_minislab.slab512x256x128_usage.slab128_usage == 0b1111) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab128_usage);
			challoc_minislab.slab512x256x128_usage.slab128_usage |= 1U << index;
			return challoc_minislab.slab128[index];
		}
		case 8: {
			if (challoc_minislab.slab512x256x128_usage.slab256_usage == 0b11) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab256_usage);
			challoc_minislab.slab512x256x128_usage.slab256_usage |= 1U << index;
			return challoc_minislab.slab256[index];
		}
		case 9: {
			if (challoc_minislab.slab512x256x128_usage.slab512_usage == 0b1) {
				return NULL;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab512_usage);
			challoc_minislab.slab512x256x128_usage.slab512_usage |= 1U << index;
			return challoc_minislab.slab512[index];
		}
		default: {
			assert(false);
		}
	}
}

bool ptr_comes_from_minislab(void* ptr) {
	size_t minislab_begin = (size_t)&challoc_minislab;
	size_t minislab_end   = minislab_begin + sizeof(MiniSlab);
	size_t ptr_addr	      = (size_t)ptr;
	return ptr_addr >= minislab_begin && ptr_addr < minislab_end;
}

size_t minislab_ptr_size(void* ptr) {
	assert(ptr_comes_from_minislab(ptr));
	ssize_t offset = (ssize_t)ptr - (ssize_t)&challoc_minislab;
	if (offset >= 0 && offset < 512) {
		return 512;
	}
	else if (offset >= 512 && offset < 1024) {
		return 256;
	}
	else if (offset >= 1024 && offset < 1536) {
		return 128;
	}
	else if (offset >= 1536 && offset < 2048) {
		return 64;
	}
	else if (offset >= 2048 && offset < 2560) {
		return 32;
	}
	else if (offset >= 2560 && offset < 3072) {
		return 16;
	}
	else if (offset >= 3072 && offset < 3584) {
		return 8;
	}
	else if (offset >= 3584 && offset < 4096) {
		return 4;
	}
	else {
		assert(false);
	}
}

void minislab_free(void* ptr) {
	assert(ptr_comes_from_minislab(ptr));

	ssize_t offset = (ssize_t)ptr - (ssize_t)&challoc_minislab;
	if (offset >= 0 && offset < 512) {
		challoc_minislab.slab512x256x128_usage.slab512_usage &= ~(1U << offset);
	}
	else if (offset >= 512 && offset < 1024) {
		challoc_minislab.slab512x256x128_usage.slab256_usage &= ~(1U << (offset - 512));
	}
	else if (offset >= 1024 && offset < 1536) {
		challoc_minislab.slab512x256x128_usage.slab128_usage &= ~(1U << (offset - 1024));
	}
	else if (offset >= 1536 && offset < 2048) {
		challoc_minislab.slab64_usage &= ~(1U << (offset - 1536));
	}
	else if (offset >= 2048 && offset < 2560) {
		challoc_minislab.slab32_usage &= ~(1U << (offset - 2048));
	}
	else if (offset >= 2560 && offset < 3072) {
		challoc_minislab.slab16_usage &= ~(1U << (offset - 2560));
	}
	else if (offset >= 3072 && offset < 3584) {
		challoc_minislab.slab8_usage &= ~(1U << (offset - 3072));
	}
	else if (offset >= 3584 && offset < 4096) {
		challoc_minislab.slab_small_usage &= ~(1U << (offset - 3584));
	}
	else {
		assert(false);
	}
}

typedef struct {
	size_t size;
} AllocMetadata;

AllocMetadata* challoc_get_metadata(void* ptr) {
	return (AllocMetadata*)(ptr - sizeof(AllocMetadata));
}

void* __chamalloc(size_t size) {
	// Try to allocate from the minislab
	ClosePowerOfTwo close_pow2 = is_close_to_power_of_two(size);
	if (close_pow2.is_close) {
		pthread_mutex_lock(&challoc_mutex);
		void* ptr = minislab_alloc(close_pow2);
		pthread_mutex_unlock(&challoc_mutex);
		if (ptr != NULL) {
			return ptr;
		}
	}

	// Allocate memory for the metadata + user data
	pthread_mutex_lock(&challoc_mutex);
	size	  = size + sizeof(AllocMetadata);
	void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	pthread_mutex_unlock(&challoc_mutex);

	// Check for errors
	if (ptr == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	// Write the size of the memory block into the metadata	²
	AllocMetadata* metadata = (AllocMetadata*)ptr;
	metadata->size		= size;

	// Return a pointer to the user data
	return ptr + sizeof(AllocMetadata);
}

void __chafree(void* ptr) {
	// Check if the pointer comes from the minislab
	if (ptr_comes_from_minislab(ptr)) {
		pthread_mutex_lock(&challoc_mutex);
		minislab_free(ptr);
		pthread_mutex_unlock(&challoc_mutex);
		return;
	}

	if (ptr == NULL) {
		return;
	}

	// Get the metadata
	AllocMetadata* metadata = challoc_get_metadata(ptr);

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

void* __charealloc(void* ptr, size_t new_size) {
	if (ptr == NULL) {
		return __chamalloc(new_size);
	}

	// Get the old metadata
	size_t old_size = 0;
	if (ptr_comes_from_minislab(ptr)) {
		old_size = minislab_ptr_size(ptr);
	}
	else {
		AllocMetadata* metadata = challoc_get_metadata(ptr);
		old_size		= metadata->size - sizeof(AllocMetadata);
	}

	// Allocate new memory
	void* new_ptr = __chamalloc(new_size);
	if (new_ptr == NULL) {
		return NULL;
	}

	// Copy the old data
	size_t min_size = old_size < new_size ? old_size : new_size;
	memcpy(new_ptr, ptr, min_size);

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
	pthread_mutex_lock(&alloc_list_mutex);
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
	pthread_mutex_unlock(&alloc_list_mutex);
}

void AllocList_remove_ptr(AllocList* list, void* ptr) {
	pthread_mutex_lock(&alloc_list_mutex);
	for (size_t i = 0; i < list->size; i++) {
		if (list->ptrs[i].ptr == ptr) {
			list->ptrs[i] = list->ptrs[list->size - 1];
			list->size--;
			AllocTrace_free(list->ptrs[list->size]);
			pthread_mutex_unlock(&alloc_list_mutex);
			return;
		}
	}
	pthread_mutex_unlock(&alloc_list_mutex);
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
		exit(EXIT_FAILURE);
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