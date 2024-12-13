#include "challoc.h"
#include "sys/types.h"
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

#define ALL_ONES(type) (type) ~(type)0

void* minislab_alloc(ClosePowerOfTwo size) {
	assert(size.is_close);
	assert(size.ceil_pow2 >= 2 && size.ceil_pow2 <= 9);

	switch (size.ceil_pow2) {
		case 2: {
			if (challoc_minislab.slab_small_usage == ALL_ONES(uint64_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab_small_usage);
			challoc_minislab.slab_small_usage |= (uint64_t)1ULL << index;
			return challoc_minislab.slab_small[index];
		}
		case 3: {
			if (challoc_minislab.slab8_usage == ALL_ONES(uint64_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab8_usage);
			challoc_minislab.slab8_usage |= 1ULL << index;
			return challoc_minislab.slab8[index];
		}
		case 4: {
			if (challoc_minislab.slab16_usage == ALL_ONES(uint32_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab16_usage);
			challoc_minislab.slab16_usage |= 1ULL << index;
			return challoc_minislab.slab16[index];
		}
		case 5: {
			if (challoc_minislab.slab32_usage == ALL_ONES(uint16_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab32_usage);
			challoc_minislab.slab32_usage |= 1ULL << index;
			return challoc_minislab.slab32[index];
		}
		case 6: {
			if (challoc_minislab.slab64_usage == ALL_ONES(uint8_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab64_usage);
			challoc_minislab.slab64_usage |= 1ULL << index;
			return challoc_minislab.slab64[index];
		}
		case 7: {
			if (challoc_minislab.slab512x256x128_usage.slab128_usage == 0b1111) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab128_usage);
			challoc_minislab.slab512x256x128_usage.slab128_usage |= 1ULL << index;
			return challoc_minislab.slab128[index];
		}
		case 8: {
			if (challoc_minislab.slab512x256x128_usage.slab256_usage == 0b11) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab256_usage);
			challoc_minislab.slab512x256x128_usage.slab256_usage |= 1ULL << index;
			return challoc_minislab.slab256[index];
		}
		case 9: {
			if (challoc_minislab.slab512x256x128_usage.slab512_usage == 0b1) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab512_usage);
			challoc_minislab.slab512x256x128_usage.slab512_usage |= 1ULL << index;
			return challoc_minislab.slab512[index];
		}
		default: {
			assert(false);
		}
	}
	return NULL;
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
	assert(offset < 4096);

	static const int sizes[]      = {512, 256, 128, 64, 32, 16, 8, 4};
	static const int thresholds[] = {512, 1024, 1536, 2048, 2560, 3072, 3584, 4096};

	if (offset >= 0 && offset < thresholds[0]) {
		return 512;
	}
	for (int i = 1; i < 8; i++) {
		if (offset >= thresholds[i - 1] && offset < thresholds[i]) {
			return sizes[i];
		}
	}
}

void minislab_free(void* ptr) {
	assert(ptr_comes_from_minislab(ptr));
	ssize_t offset = (ssize_t)ptr - (ssize_t)&challoc_minislab;
	assert(offset < 4096);

	// Set the appropriate bitmask to 0 to signify it can be used
	size_t layer = offset / 512;
	switch (layer) {
		case 0:
			challoc_minislab.slab512x256x128_usage.slab512_usage &= ~((1U << offset) - (512 * 0));
			break;
		case 1:
			challoc_minislab.slab512x256x128_usage.slab256_usage &= ~((1U << offset) - (512 * 1));
			break;
		case 2:
			challoc_minislab.slab512x256x128_usage.slab128_usage &= ~((1U << offset) - (512 * 2));
			break;
		case 3:
			challoc_minislab.slab64_usage &= ~((1U << offset) - (512 * 3));
			break;
		case 4:
			challoc_minislab.slab32_usage &= ~((1U << offset) - (512 * 4));
			break;
		case 5:
			challoc_minislab.slab16_usage &= ~((1U << offset) - (512 * 5));
			break;
		case 6:
			challoc_minislab.slab8_usage &= ~((1U << offset) - (512 * 6));
			break;
		case 7:
			challoc_minislab.slab_small_usage &= ~((1U << offset) - (512 * 7));
			break;
		default:
			assert(false);
	}
}

/// ------------------------------------------------
/// Fragmentation and block allocation
/// ------------------------------------------------

// Double linked list to keep track of allocated memory
typedef struct AllocMetadata AllocMetadata;
struct AllocMetadata {
	size_t size;	     // Size of the allocated memory
	AllocMetadata* next; // Next block in the linked list
	AllocMetadata* prev; // Previous block in the linked list
	size_t block_idx;    // In which block the memory is allocated
};

void AllocMetadata_print(AllocMetadata* metadata) {
	char prev[200];
	char next[200];
	if (metadata->prev == NULL) {
		sprintf(prev, "NULL");
	}
	else {
		sprintf(prev, "[%p | %zu]", metadata->prev, metadata->prev->size);
	}
	if (metadata->next == NULL) {
		sprintf(next, "NULL");
	}
	else {
		sprintf(next, "[%p | %zu]", metadata->next, metadata->next->size);
	}

	printf("[Block %zu] 	%s <-> [%p | %zu] <-> %s\n", metadata->block_idx, prev, metadata, metadata->size, next);
}

AllocMetadata* challoc_get_metadata(void* ptr) {
	return (AllocMetadata*)(ptr - sizeof(AllocMetadata));
}

typedef struct {
	size_t size;		// Size of the mmap block
	size_t free_space;	// Space left in the mmap block
	AllocMetadata* head;	// Head of the linked list
	AllocMetadata* tail;	// Tail of the linked list
	void* mmap_ptr;		// Pointer to the mmap block
	uint8_t time_to_live;	// Number of mallocs in which this block wasn't reused before it is truly unmapped
	bool freshly_allocated; // True if the block was just allocated (and therefore full of 0)
} MmapBlock;

typedef struct {
	MmapBlock* blocks;
	size_t size;
	size_t capacity;
} MmapBlockList;

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

MmapBlockList MmapBlockList_with_capacity(size_t capacity) {
	return (MmapBlockList){
	    .blocks   = mmap(NULL, capacity * sizeof(MmapBlock), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
	    .size     = 0,
	    .capacity = capacity,
	};
}

void MmapBlockList_destroy(MmapBlockList* list) {
	munmap(list->blocks, list->capacity * sizeof(MmapBlock));
}

void MmapBlockList_allocate_new_block(MmapBlockList* list, size_t size_requested) {
	size_t size = size_requested;
	size	    = ceil_to_4096multiple(size);
	void* ptr   = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	// Add the block to the list
	if (list->size == list->capacity) {
		list->capacity *= 2;
		MmapBlock* new_blocks =
		    mmap(NULL, list->capacity * sizeof(MmapBlock), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		memcpy(new_blocks, list->blocks, list->size * sizeof(MmapBlock));
		munmap(list->blocks, list->size * sizeof(MmapBlock));
		list->blocks = new_blocks;
	}
	list->blocks[list->size] = (MmapBlock){
	    .size = size, .free_space = size, .head = NULL, .tail = NULL, .freshly_allocated = true, .mmap_ptr = ptr,
	    //     .mmap_end	       = ptr + size,
	};
	list->size++;
}

size_t AllocMetadata_space_between(AllocMetadata* a, AllocMetadata* b) {
	assert(b >= a);
	return (size_t)b - (size_t)a - sizeof(AllocMetadata) - a->size;
}

// Create a global list of mmap blocks
MmapBlockList challoc_mmap_blocks  = {0};
MmapBlockList challoc_freed_blocks = {0};

void MmapBlock_print(MmapBlock* block) {
	printf("Block n°%zu (%zu / %zu bytes) : ", block - challoc_mmap_blocks.blocks, block->free_space, block->size);
	for (AllocMetadata* current = block->head; current != NULL; current = current->next) {
		assert(current->block_idx == block - challoc_mmap_blocks.blocks);
		printf("[[%zu] %p | %zu] -> ", current->block_idx, current, current->size);
	}
	printf("x\n");
}

void MmapBlockList_print(MmapBlockList* list) {
	printf("MmapBlockList (%zu / %zu) : \n", list->size, list->capacity);
	for (size_t i = 0; i < list->size; i++) {
		MmapBlock_print(&list->blocks[i]);
	}
}

void MmapBlockList_push(MmapBlockList* list, MmapBlock block) {
	if (list->size == list->capacity) {
		list->capacity *= 2;
		size_t size	      = list->capacity * sizeof(MmapBlock);
		size		      = ceil_to_4096multiple(size);
		MmapBlock* new_blocks = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		memcpy(new_blocks, list->blocks, list->size * sizeof(MmapBlock));
		if (munmap(list->blocks, list->size * sizeof(MmapBlock)) == -1) {
			perror("munmap");
		}
		list->blocks = new_blocks;
	}
	list->blocks[list->size] = block;
	list->size++;
}

void MmapBlockList_push_or_remove(MmapBlockList* list, MmapBlock block) {
	if (list->size == list->capacity) {
		if (munmap(block.mmap_ptr, block.size) == -1) {
			perror("munmap");
		}
		return;
	}
	list->blocks[list->size] = block;
	list->size++;
}

bool MmapBlock_has_enough_space(MmapBlock* block, size_t size_requested) {
	return block->free_space >= size_requested + sizeof(AllocMetadata);
}

void MmapBlockList_destroy_whole_list(MmapBlockList* list) {
	for (size_t i = 0; i < list->size; i++) {
		if (list->blocks[i].head != NULL) {
			if (munmap(list->blocks[i].mmap_ptr - sizeof(MmapBlock), list->blocks[i].size) == -1) {
				printf("Error while freeing block %zu\n", i);
				perror("munmap");
			}
		}
	}
	if (munmap(list->blocks, list->capacity * sizeof(MmapBlock)) == -1) {
		printf("Error while freeing the list\n");
		perror("munmap");
	}
}

MmapBlock* MmapBlockList_peek(MmapBlockList* list, size_t block_idx) {
	assert(block_idx <= list->size);
	return &list->blocks[block_idx];
}

void* MmapBlock_try_allocate(MmapBlockList* list, size_t block_idx, size_t size_requested) {
	assert(block_idx <= list->size);
	assert(list->blocks[block_idx].free_space <= list->blocks[block_idx].size);
	size_t size_needed = size_requested + sizeof(AllocMetadata);

	// We will never be able to find a space
	MmapBlock* block = &list->blocks[block_idx];
	if (block->free_space < size_needed) {
		return NULL;
	}

	// First allocated block
	AllocMetadata* last = block->tail;
	if (last == NULL) {
		block->head	       = (AllocMetadata*)(block->mmap_ptr);
		block->head->size      = size_requested;
		block->head->next      = NULL;
		block->head->prev      = NULL;
		block->head->block_idx = block_idx;
		block->tail	       = block->head;
		block->free_space -= size_needed;
		assert(block->free_space <= block->size);
		block->freshly_allocated = false;
		return (uint8_t*)(block->head) + sizeof(AllocMetadata);
	}

	// Go through the linked list to find a space
	for (AllocMetadata* current = block->head; current != NULL; current = current->next) {
		AllocMetadata* next = current->next;
		if (next == NULL) { // We are at the end of the linked list
			size_t space_between_last_and_end_of_block =
			    ((size_t)block->mmap_ptr + block->size) - (size_t)current - sizeof(AllocMetadata) - current->size;
			if (space_between_last_and_end_of_block >= size_needed) {
				AllocMetadata* new_metadata = (AllocMetadata*)((size_t)current + sizeof(AllocMetadata) + current->size);
				new_metadata->size	    = size_requested;
				new_metadata->next	    = NULL;
				new_metadata->prev	    = current;
				new_metadata->block_idx	    = block_idx;

				current->next = new_metadata;
				block->tail   = new_metadata;
				block->free_space -= size_needed;
				assert(block->free_space <= block->size);
				block->freshly_allocated = false;
				return (uint8_t*)(new_metadata) + sizeof(AllocMetadata);
			}
		}
		else { // We are in the middle of the linked list
			ssize_t space_between_current_and_next = (ssize_t)next - (ssize_t)current - sizeof(AllocMetadata) - current->size;
			if (space_between_current_and_next >= size_needed) { // We can allocate in between
				AllocMetadata* new_metadata = (AllocMetadata*)((size_t)current + sizeof(AllocMetadata) + current->size);
				new_metadata->size	    = size_requested;
				new_metadata->next	    = next;
				new_metadata->prev	    = current;
				new_metadata->block_idx	    = block_idx;

				current->next = new_metadata;
				next->prev    = new_metadata;
				block->free_space -= size_needed;
				assert(block->free_space <= block->size);
				block->freshly_allocated = false;
				return (uint8_t*)(new_metadata) + sizeof(AllocMetadata);
			}
		}
	}

	return NULL;
}

void MmapBlock_free(MmapBlock* block, AllocMetadata* ptr) {
	AllocMetadata* current = block->head;

	// Check if the only block is the one we want to free
	if (current == ptr) {
		block->head = current->next;
		if (block->head == NULL) {
			block->tail = NULL;
		}
		block->free_space += current->size + sizeof(AllocMetadata);
		assert(block->free_space <= block->size);
		return;
	}

	// Remove the metadata from the linked list
	if (ptr->prev != NULL) {
		ptr->prev->next = ptr->next;
	}
	if (ptr->next != NULL) {
		ptr->next->prev = ptr->prev;
	}

	// Update the head and tail
	if (ptr == block->head) {
		block->head = ptr->next;
	}

	if (ptr == block->tail) {
		block->tail = ptr->prev;
	}

	// Update the free space
	block->free_space += ptr->size + sizeof(AllocMetadata);
	assert(block->free_space <= block->size);
}

void MmapBlockList_free(AllocMetadata* ptr) {
	size_t block_idx = ptr->block_idx;
	MmapBlock* block = &challoc_mmap_blocks.blocks[block_idx];
	MmapBlock_free(block, ptr);

	// Check if the block is empty
	if (block->free_space == block->size) {
		assert(block->head == NULL);

		// Push the block to the freed list
		MmapBlockList_push_or_remove(&challoc_freed_blocks, *block);

		// Remove and swap the block from the allocated
		if (challoc_mmap_blocks.size == 1) {
			// The list is empty
			challoc_mmap_blocks.size = 0;
			return;
		}

		// Move the last block to the empty block
		MmapBlock* last_block		      = &challoc_mmap_blocks.blocks[challoc_mmap_blocks.size - 1];
		challoc_mmap_blocks.blocks[block_idx] = *last_block;
		challoc_mmap_blocks.size--;
		for (AllocMetadata* current = block->head; current != NULL; current = current->next) {
			current->block_idx = block_idx;
		}
	}
}

void decrease_ttl_and_unmap() {
	for (size_t i = 0; i < challoc_freed_blocks.size; i++) {
		assert(challoc_freed_blocks.blocks[i].head == NULL);
		MmapBlock* block = &challoc_freed_blocks.blocks[i];
		if (block->time_to_live == 1) {
			if (munmap(block->mmap_ptr, block->size) == -1) {
				perror("munmap");
			}
			// Move the last block to the empty block
			MmapBlock* last_block	       = &challoc_freed_blocks.blocks[challoc_freed_blocks.size - 1];
			challoc_freed_blocks.blocks[i] = *last_block;
			challoc_freed_blocks.size--;
			i--;
		}
		else {
			block->time_to_live--;
		}
	}
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
			decrease_ttl_and_unmap();
			return ptr;
		}
	}

	// Go through the list of mmap blocks
	for (size_t i = 0; i < challoc_mmap_blocks.size; i++) {
		MmapBlock* block = &challoc_mmap_blocks.blocks[i];
		if (MmapBlock_has_enough_space(block, size)) {
			void* ptr = MmapBlock_try_allocate(&challoc_mmap_blocks, i, size);
			if (ptr != NULL) {
				decrease_ttl_and_unmap();
				return ptr;
			}
		}
	}

	// Go through the list of freed blocks
	for (size_t i = 0; i < challoc_freed_blocks.size; i++) {
		MmapBlock block = challoc_freed_blocks.blocks[i];
		if (MmapBlock_has_enough_space(&block, size)) {
			MmapBlock* last_block	       = &challoc_freed_blocks.blocks[challoc_freed_blocks.size - 1];
			challoc_freed_blocks.blocks[i] = *last_block;
			challoc_freed_blocks.size--;

			// Revive the block into a ready-to-use one
			block.time_to_live = time_to_live_with_size(block.size);
			block.head	   = NULL;
			block.tail	   = NULL;
			block.free_space   = block.size;
			MmapBlockList_push(&challoc_mmap_blocks, block);
			void* ptr = MmapBlock_try_allocate(&challoc_mmap_blocks, challoc_mmap_blocks.size - 1, size);
			decrease_ttl_and_unmap();
			return ptr;
		}
	}

	// No block had enough space, create a new one
	MmapBlockList_allocate_new_block(&challoc_mmap_blocks, size + sizeof(AllocMetadata) + sizeof(MmapBlock));

	// Check if we could allocate the new one
	if (challoc_mmap_blocks.blocks == MAP_FAILED) {
		return NULL;
	}

	void* ptr = MmapBlock_try_allocate(&challoc_mmap_blocks, challoc_mmap_blocks.size - 1, size);
	decrease_ttl_and_unmap();

	return ptr;
}

void __chafree(void* ptr) {
	if (ptr == NULL) {
		return;
	}

	// Check if the pointer comes from the minislab before getting wrong metadata
	if (ptr_comes_from_minislab(ptr)) {
		minislab_free(ptr);
		return;
	}

	AllocMetadata* metadata = challoc_get_metadata(ptr);
	MmapBlockList_free(metadata);
}

void* __chacalloc(size_t nmemb, size_t size) {
	void* ptr = __chamalloc(nmemb * size);

	if (ptr_comes_from_minislab(ptr)) {
		memset(ptr, 0, nmemb * size);
		return ptr;
	}

	// Check if it comes from a freshly allocated block
	AllocMetadata* metadata = challoc_get_metadata(ptr);
	size_t block_idx	= metadata->block_idx;
	MmapBlock* block	= &challoc_mmap_blocks.blocks[block_idx];
	if (block->freshly_allocated) { // No need to memset, the block is already zeroed
		for (size_t i = 0; i < nmemb * size; i++) {
			assert(((uint8_t*)ptr)[i] == 0);
		}
		return ptr;
	}

	// Set memory to zero
	if (ptr != NULL) {
		memset(ptr, 0, nmemb * size);
	}

	return ptr;
}

void* __charealloc(void* ptr, size_t new_size) {
	// If ptr is NULL, behave like malloc
	if (ptr == NULL) {
		return __chamalloc(new_size);
	}

	// Get the size of the allocation
	size_t old_size = 0;
	if (ptr_comes_from_minislab(ptr)) {
		old_size = minislab_ptr_size(ptr);
	}
	else {
		AllocMetadata* metadata = challoc_get_metadata(ptr);
		old_size		= metadata->size;
	}

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
	challoc_mmap_blocks  = MmapBlockList_with_capacity(30);
	challoc_freed_blocks = MmapBlockList_with_capacity(10);

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
	MmapBlockList_destroy(&challoc_mmap_blocks);
	MmapBlockList_destroy(&challoc_freed_blocks);
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