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

pthread_mutex_t challoc_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MUTEX(code, mutex)                                                                                                                 \
	pthread_mutex_lock(&(mutex));                                                                                                      \
	code;                                                                                                                              \
	pthread_mutex_unlock(&(mutex));

#define CHALLOC_MUTEX(code) MUTEX(code, challoc_mutex)

/// ------------------------------------------------
/// Slab allocator
/// ------------------------------------------------
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

void MiniSlab_print_usage(MiniSlab slab) {
	printf("slab_small_usage: %lx\n", slab.slab_small_usage);
	printf("slab8_usage: %lx\n", slab.slab8_usage);
	printf("slab16_usage: %x\n", slab.slab16_usage);
	printf("slab32_usage: %x\n", slab.slab32_usage);
	printf("slab64_usage: %x\n", slab.slab64_usage);
	printf("slab512x256x128_usage: %x\n", (uint8_t)slab.slab512x256x128_usage.slab512_usage);
}

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

	void* ptr = NULL;
	switch (size.ceil_pow2) {
		case 2: {
			if (challoc_minislab.slab_small_usage == 0xFFFFFFFF) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab_small_usage);
			challoc_minislab.slab_small_usage |= 1ULL << index;
			ptr = challoc_minislab.slab_small[index];
			break;
		}
		case 3: {
			if (challoc_minislab.slab8_usage == 0xFFFFFFFF) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab8_usage);
			challoc_minislab.slab8_usage |= 1ULL << index;
			ptr = challoc_minislab.slab8[index];
			break;
		}
		case 4: {
			if (challoc_minislab.slab16_usage == 0xFFFF) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab16_usage);
			challoc_minislab.slab16_usage |= 1UL << index;
			ptr = challoc_minislab.slab16[index];
			break;
		}
		case 5: {
			if (challoc_minislab.slab32_usage == 0xFF) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab32_usage);
			challoc_minislab.slab32_usage |= 1U << index;
			ptr = challoc_minislab.slab32[index];
			break;
		}
		case 6: {
			if (challoc_minislab.slab64_usage == 0xF) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab64_usage);
			challoc_minislab.slab64_usage |= 1U << index;
			ptr = challoc_minislab.slab64[index];
			break;
		}
		case 7: {
			if (challoc_minislab.slab512x256x128_usage.slab128_usage == 0b1111) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab128_usage);
			challoc_minislab.slab512x256x128_usage.slab128_usage |= 1U << index;
			ptr = challoc_minislab.slab128[index];
			break;
		}
		case 8: {
			if (challoc_minislab.slab512x256x128_usage.slab256_usage == 0b11) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab256_usage);
			challoc_minislab.slab512x256x128_usage.slab256_usage |= 1U << index;
			ptr = challoc_minislab.slab256[index];
			break;
		}
		case 9: {
			if (challoc_minislab.slab512x256x128_usage.slab512_usage == 0b1) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab512_usage);
			challoc_minislab.slab512x256x128_usage.slab512_usage |= 1U << index;
			ptr = challoc_minislab.slab512[index];
			break;
		}
		default: {
			assert(false);
		}
	}
	return ptr;
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

/// ------------------------------------------------
/// Reuse allocations
/// ------------------------------------------------

typedef struct {
	size_t size;		// Size of the allocation
	bool freshly_allocated; // True if the allocation was just made
	// bool freed;		// True if the allocation was freed
	uint8_t time_to_live; // How many times do we call malloc of free before unmapping this allocation
	void* mmap_addr;      // Address of the mmap
} AllocMetadata;

typedef struct {
	size_t size;
	size_t capacity;
	AllocMetadata** blocks;
} AllocMetadataList;

size_t ceil_to_4096multiple(size_t size) {
	size_t remainder = size % 4096;
	if (remainder == 0) {
		return size;
	}
	return size + 4096 - remainder;
}

size_t time_to_live_with_size(size_t size) {
	// the bigger the block, the less time we keep it alive
	size_t ceil = ceil_to_4096multiple(size);
	size_t log2 = 4096;
	size_t ttl  = 5;
	while (log2 < ceil) {
		log2 *= 2;
		ttl--;
		if (ttl == 1) {
			break;
		}
	}
	return ttl;
}

void relive_allocation(AllocMetadata* metadata) {
	// Reset the time to live
	metadata->time_to_live = time_to_live_with_size(metadata->size);
}

AllocMetadataList AllocMetadataList_with_capacity(size_t capacity) {
	return (AllocMetadataList){
	    .blocks   = mmap(NULL, capacity * sizeof(AllocMetadata*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
	    .size     = 0,
	    .capacity = capacity,
	};
}

void AllocMetadataList_push_or_unmap(AllocMetadataList* list, AllocMetadata* metadata) {
	if (list->size == list->capacity) {
		// Just unmmap the metadata
		if (munmap(metadata->mmap_addr, metadata->size) == -1) {
			perror("munmap");
		}
		return;
	}
	// printf("Pushing a new block\n");
	list->blocks[list->size] = metadata;
	list->size++;
	// printf("list->size: %zu\n", list->size);
}

AllocMetadata* AllocMetadataList_pop_at(AllocMetadataList* list, size_t index) {
	assert(index < list->size);
	AllocMetadata* metadata = list->blocks[index];
	// Swap the last element with the one we want to remove
	list->blocks[index] = list->blocks[list->size - 1];
	list->size--;
	return metadata;
}

void AllocMetadataList_remove_at(AllocMetadataList* list, size_t index) {
	assert(index < list->size);
	// Swap the last element with the one we want to remove, and unmap the last element
	if (munmap(list->blocks[index]->mmap_addr, list->blocks[index]->size) == -1) {
		perror("munmap");
	}
	list->blocks[index] = list->blocks[list->size - 1];
	list->size--;
}

AllocMetadata* AllocMetadataList_get(AllocMetadataList* list, size_t index) {
	assert(index < list->size);
	return list->blocks[index];
}

void AllocMetadataList_decrement_TTL_and_unmap(AllocMetadataList* list) {
	for (size_t i = 0; i < list->size; i++) {
		AllocMetadata* metadata = AllocMetadataList_get(list, i);
		// printf("metadata->time_to_live before: %d\n", metadata->time_to_live);
		metadata->time_to_live--;
		assert(metadata->time_to_live <= 10);
		if (metadata->time_to_live == 0) {
			AllocMetadataList_remove_at(list, i);
			i--;
		}
	}
}

AllocMetadataList freed = {0};

void AllocMetadataList_destroy(AllocMetadataList* list) {
	munmap(list->blocks, list->capacity * sizeof(AllocMetadataList));
}

AllocMetadata* challoc_get_metadata(void* ptr) {
	return (AllocMetadata*)(ptr - sizeof(AllocMetadata));
}

size_t AllocMetadata_get_size(AllocMetadata* metadata) {
	return metadata->size;
}

void* __chamalloc(size_t size) {
	if (size == 0) {
		return NULL;
	}

	// Try to allocate from the minislab
	ClosePowerOfTwo close_pow2 = is_close_to_power_of_two(size);
	if (close_pow2.is_close) {
		void* ptr = minislab_alloc(close_pow2);
		if (ptr != NULL) {
			return ptr;
		}
	}

	// Go through the list of freed allocations
	for (size_t i = 0; i < freed.size; i++) {
		AllocMetadata* metadata = AllocMetadataList_get(&freed, i);
		if (metadata->size >= size) {
			AllocMetadataList_pop_at(&freed, i);
			relive_allocation(metadata);
			// Decrement the time to live of the freed blocks
			AllocMetadataList_decrement_TTL_and_unmap(&freed);
			return metadata->mmap_addr + sizeof(AllocMetadata);
		}
	}

	// Allocate a new block
	void* mmap_addr = mmap(NULL, size + sizeof(AllocMetadata), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_addr == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	// Write the metadata
	AllocMetadata* metadata = (AllocMetadata*)mmap_addr;
	*metadata		= (AllocMetadata){
			  .size		     = size,
			  .freshly_allocated = true,
			  .time_to_live	     = time_to_live_with_size(size),
			  .mmap_addr	     = mmap_addr,
	      };

	AllocMetadataList_decrement_TTL_and_unmap(&freed);

	return metadata->mmap_addr + sizeof(AllocMetadata);
}

void __chafree(void* ptr) {
	if (ptr == NULL) {
		return;
	}

	AllocMetadataList_decrement_TTL_and_unmap(&freed);

	// Check if the pointer comes from the minislab
	if (ptr_comes_from_minislab(ptr)) {
		minislab_free(ptr);
		return;
	}

	// Get the metadata
	AllocMetadata* metadata	    = challoc_get_metadata(ptr);
	metadata->freshly_allocated = false;

	AllocMetadataList_push_or_unmap(&freed, metadata);
}

void* __chacalloc(size_t nmemb, size_t size) {
	// Allocate memory
	void* ptr = __chamalloc(nmemb * size);
	if (ptr == NULL) {
		return NULL;
	}

	if (ptr_comes_from_minislab(ptr)) {
		memset(ptr, 0, nmemb * size);
		return ptr;
	}

	// Check if it comes from a freshly allocated block
	AllocMetadata* metadata = challoc_get_metadata(ptr);
	if (metadata->freshly_allocated) {
		// Don't need to set memory to zero, OS already does it
		return ptr;
	}

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
		old_size		= metadata->size;
	}

	// Allocate new memory
	void* new_ptr = __chamalloc(new_size);
	if (new_ptr == NULL) {
		return NULL;
	}

	// Copy the old data to the new allocation
	size_t copy_size = old_size < new_size ? old_size : new_size;
	memcpy(new_ptr, ptr, copy_size);

	// Free the old allocation
	__chafree(ptr);

	return new_ptr;
}

#ifdef CHALLOC_LEAKCHECK

typedef struct {
	void* ptr;
	size_t size;
} AllocTrace;

void AllocTrace_free(AllocTrace trace) {}

typedef struct {
	AllocTrace* ptrs;
	size_t size;
	size_t capacity;
} AllocLeakcheckTraceList;

AllocLeakcheckTraceList AllocLeakcheckTraceList_with_capacity(size_t capacity) {
	return (AllocLeakcheckTraceList){
	    .ptrs     = mmap(NULL, capacity * sizeof(AllocTrace), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
	    .size     = 0,
	    .capacity = capacity,
	};
}

void AllocLeakcheckTraceList_push(AllocLeakcheckTraceList* list, void* ptr, size_t ptr_size) {
	if (list->size == list->capacity) {
		list->capacity *= 2;
		AllocTrace* new_ptrs =
		    mmap(NULL, list->capacity * sizeof(AllocTrace), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		memcpy(new_ptrs, list->ptrs, list->size * sizeof(AllocTrace));
		munmap(list->ptrs, list->size * sizeof(AllocTrace));
		list->ptrs = new_ptrs;
	}
	list->ptrs[list->size] = (AllocTrace){
	    .ptr  = ptr,
	    .size = ptr_size,
	};
	list->size++;
}

void AllocLeakcheckTraceList_remove_ptr(AllocLeakcheckTraceList* list, void* ptr) {
	for (size_t i = 0; i < list->size; i++) {
		if (list->ptrs[i].ptr == ptr) {
			list->ptrs[i] = list->ptrs[list->size - 1];
			list->size--;
			AllocTrace_free(list->ptrs[list->size]);
			return;
		}
	}
}

void AllocLeakcheckTraceList_free(AllocLeakcheckTraceList* list) {
	for (size_t i = 0; i < list->size; i++) {
		AllocTrace_free(list->ptrs[i]);
	}
	munmap(list->ptrs, list->size * sizeof(AllocTrace));
}

AllocLeakcheckTraceList challoc_allocs = {0};
#endif

void __attribute__((constructor)) init() {
	freed = AllocMetadataList_with_capacity(10);
#ifdef CHALLOC_LEAKCHECK
	challoc_allocs = AllocLeakcheckTraceList_with_capacity(10);
#endif
}

void __attribute__((destructor)) fini() {
#ifdef CHALLOC_LEAKCHECK
	if (challoc_allocs.size > 0) {
		fprintf(stderr, "challoc: detected %zu memory leaks\n", challoc_allocs.size);
		for (size_t i = 0; i < challoc_allocs.size; i++) {
			fprintf(stderr, "Leaked memory at %p, Content: ", challoc_allocs.ptrs[i].ptr);
			size_t ptr_size = challoc_allocs.ptrs[i].size;
			for (size_t j = 0; j < ptr_size; j++) {
				fprintf(stderr, "%02X ", ((uint8_t*)challoc_allocs.ptrs[i].ptr)[j]);
			}
			fprintf(stderr, "\n");
		}
		exit(EXIT_FAILURE);
	}

	printf("challoc: no memory leaks detected, congrats!\n");
	printf(" /\\_/\\\n>(^w^)<\n  b d\n");

	AllocLeakcheckTraceList_free(&challoc_allocs);
#endif
	AllocMetadataList_destroy(&freed);
}

void* chamalloc(size_t size) {
	void* ptr;
	CHALLOC_MUTEX({
		ptr = __chamalloc(size);
#ifdef CHALLOC_LEAKCHECK
		AllocLeakcheckTraceList_push(&challoc_allocs, ptr, size);
#endif
	})
	return ptr;
}

void chafree(void* ptr) {
	CHALLOC_MUTEX({
		__chafree(ptr);
#ifdef CHALLOC_LEAKCHECK
		AllocLeakcheckTraceList_remove_ptr(&challoc_allocs, ptr);
#endif
	})
}

void* chacalloc(size_t nmemb, size_t size) {
	void* ptr;
	CHALLOC_MUTEX({
		ptr = __chacalloc(nmemb, size);
#ifdef CHALLOC_LEAKCHECK
		AllocLeakcheckTraceList_push(&challoc_allocs, ptr, nmemb * size);
#endif
	})
	return ptr;
}

void* charealloc(void* ptr, size_t size) {
	void* new_ptr;
	CHALLOC_MUTEX({
		new_ptr = __charealloc(ptr, size);
#ifdef CHALLOC_LEAKCHECK
		AllocLeakcheckTraceList_remove_ptr(&challoc_allocs, ptr);
		AllocLeakcheckTraceList_push(&challoc_allocs, new_ptr, size);
#endif
	})
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