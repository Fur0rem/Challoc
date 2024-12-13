/**
 * @file src/challoc.c
 * @brief Implementation of the challoc library
 */

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

/// ------------------------------------------------
/// Multithreading
/// ------------------------------------------------

/** \addtogroup Challoc_multithreading Multithreading support
 *  @{
 */

/// Mutex to protect the minislab allocator
static pthread_mutex_t challoc_mutex = PTHREAD_MUTEX_INITIALIZER;

/// Execute code while holding the challoc mutex
#define CHALLOC_MUTEX(code)                                                                                                                \
	pthread_mutex_lock(&challoc_mutex);                                                                                                \
	code;                                                                                                                              \
	pthread_mutex_unlock(&challoc_mutex);
/** @} */

/// ------------------------------------------------
/// Slab allocator
/// ------------------------------------------------

/** \defgroup Challoc_minislab Minislab Allocator
 *  @{
 */

/**
 * @brief Compact representation of the usage of the 512, 256 and 128 bytes slabs
 */
typedef struct {
	uint8_t slab512_usage : 1;
	uint8_t slab256_usage : 2;
	uint8_t slab128_usage : 4;
} Slab512x256x128Usage;

/**
 * @brief Minislab to allocate small memory regions
 * It contains 512, 256, 128, 64, 32, 16, 8 and 4 bytes layers and keeps track of the usage of each layer using bitmasks
 */
__attribute__((aligned(4096))) typedef struct {
	/// Chunks of memory
	uint8_t slab512[1][512];   ///< 1 chunk of 512 bytes
	uint8_t slab256[2][256];   ///< 2 chunks of 256 bytes
	uint8_t slab128[4][128];   ///< 4 chunks of 128 bytes
	uint8_t slab64[8][64];	   ///< 8 chunks of 64 bytes
	uint8_t slab32[16][32];	   ///< 16 chunks of 32 bytes
	uint8_t slab16[32][16];	   ///< 32 chunks of 16 bytes
	uint8_t slab8[64][8];	   ///< 64 chunks of 8 bytes
	uint8_t slab_small[64][4]; ///< 64 chunks of 4 bytes

	/// Bitmasks to keep track of the usage of each layer
	uint64_t slab_small_usage;		    ///< Usage bitmask of 4 bytes chunks
	uint64_t slab8_usage;			    ///< Usage bitmask of 8 bytes chunks
	uint32_t slab16_usage;			    ///< Usage bitmask of 16 bytes chunks
	uint16_t slab32_usage;			    ///< Usage bitmask of 32 bytes chunks
	uint8_t slab64_usage;			    ///< Usage bitmask of 64 bytes chunks
	Slab512x256x128Usage slab512x256x128_usage; ///< Usage bitmask of 512, 256 and 128 bytes chunks combined for compactness
} MiniSlab;

/// Global minislab allocator
MiniSlab challoc_minislab = {0};

/**
 * @brief Print the usage of the minislab allocator
 * @param slab The minislab allocator
 */
void minislab_print_usage(MiniSlab slab) {
	printf("slab_small_usage: %lx\n", slab.slab_small_usage);
	printf("slab8_usage: %lx\n", slab.slab8_usage);
	printf("slab16_usage: %x\n", slab.slab16_usage);
	printf("slab32_usage: %x\n", slab.slab32_usage);
	printf("slab64_usage: %x\n", slab.slab64_usage);
	printf("slab512x256x128_usage: %x\n", (uint8_t)slab.slab512x256x128_usage.slab512_usage);
}

/**
 * @brief Structure to represent if a size is close to a power of two up to 512, and contains that power of two if it is
 */
typedef struct {
	bool is_close : 1;     ///< 1 if the size is close to a power of two
	uint8_t ceil_pow2 : 5; ///< The power of two if the size is close to one, 512 is the max power of two we can reach
			       ///< we use 5 bits because 2^9 = 512, ceil(log2(512)) = 9
} ClosePowerOfTwo;

/**
 * @brief Check if a size is close to a power of two
 * @param size The size to check
 * @return A ClosePowerOfTwo structure indicating if the size is close to a power of two and the power of two if it is
 */
ClosePowerOfTwo is_close_to_power_of_two(size_t size) {
	ClosePowerOfTwo false_result = {
	    .is_close  = false,
	    .ceil_pow2 = 0,
	};

	// Will not fit in the minislab
	if (size > 512) {
		return false_result;
	}

	// Accept to allocate in the minislab to any size smaller than 4
	if (size <= 4) {
		return (ClosePowerOfTwo){
		    .is_close  = true,
		    .ceil_pow2 = 2,
		};
	}

	// Compute the smallest power of two that is greater than the size
	size_t ceil_pow2 = 2 * 2;
	size_t pow2	 = 2;
	while (ceil_pow2 < size) {
		ceil_pow2 *= 2;
		pow2++;
	}

	// If the size is close to the power of two, we accept it
	const double threshold = 1.2;
	if ((double)ceil_pow2 / (double)size <= threshold) {
		return (ClosePowerOfTwo){
		    .is_close  = true,
		    .ceil_pow2 = pow2,
		};
	}
	else {
		return false_result;
	}
}

/**
 * @brief Find the first bit set to 0 in a 64-bit value from left to right
 * @param value The value to check
 * @return The index of the first bit set to 0
 */
size_t find_first_bit_at_0(uint64_t value) {
	for (size_t i = 0; i < 64; i++) {
		if ((value & (1ULL << i)) == 0) { // Found a bit set to 0
			return i;
		}
	}
	return 0;
}

/// Helper macro to set all bits to 1 in a value
#define ALL_ONES(type) (type) ~(type)0

/**
 * @brief Allocate memory from the minislab allocator
 * @param size The size to allocate
 * @return A pointer to the allocated memory
 */
void* minislab_alloc(ClosePowerOfTwo size) {
	// If we are here, we have accepted to allocate in the minislab
	// And therefore the size is between 4 and 512
	assert(size.is_close);
	assert(size.ceil_pow2 >= 2 && size.ceil_pow2 <= 9);

	switch (size.ceil_pow2) {
		case 2: { // 4 bytes
			if (challoc_minislab.slab_small_usage == ALL_ONES(uint64_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab_small_usage);
			challoc_minislab.slab_small_usage |= (uint64_t)1ULL << index;
			return challoc_minislab.slab_small[index];
		}
		case 3: { // 8 bytes
			if (challoc_minislab.slab8_usage == ALL_ONES(uint64_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab8_usage);
			challoc_minislab.slab8_usage |= 1ULL << index;
			return challoc_minislab.slab8[index];
		}
		case 4: { // 16 bytes
			if (challoc_minislab.slab16_usage == ALL_ONES(uint32_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab16_usage);
			challoc_minislab.slab16_usage |= 1ULL << index;
			return challoc_minislab.slab16[index];
		}
		case 5: { // 32 bytes
			if (challoc_minislab.slab32_usage == ALL_ONES(uint16_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab32_usage);
			challoc_minislab.slab32_usage |= 1ULL << index;
			return challoc_minislab.slab32[index];
		}
		case 6: { // 64 bytes
			if (challoc_minislab.slab64_usage == ALL_ONES(uint8_t)) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab64_usage);
			challoc_minislab.slab64_usage |= 1ULL << index;
			return challoc_minislab.slab64[index];
		}
		case 7: { // 128 bytes
			if (challoc_minislab.slab512x256x128_usage.slab128_usage == 0b1111) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab128_usage);
			challoc_minislab.slab512x256x128_usage.slab128_usage |= 1ULL << index;
			return challoc_minislab.slab128[index];
		}
		case 8: { // 256 bytes
			if (challoc_minislab.slab512x256x128_usage.slab256_usage == 0b11) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab256_usage);
			challoc_minislab.slab512x256x128_usage.slab256_usage |= 1ULL << index;
			return challoc_minislab.slab256[index];
		}
		case 9: { // 512 bytes
			if (challoc_minislab.slab512x256x128_usage.slab512_usage == 0b1) {
				break;
			}
			size_t index = find_first_bit_at_0(challoc_minislab.slab512x256x128_usage.slab512_usage);
			challoc_minislab.slab512x256x128_usage.slab512_usage |= 1ULL << index;
			return challoc_minislab.slab512[index];
		}
		default: { // Should not reach here
			assert(false);
		}
	}

	// The minislab is full, we cannot allocate from it
	return NULL;
}

/**
 * @brief Check if a pointer comes from the minislab allocator
 * @param ptr The pointer to check
 * @return True if the pointer comes from the minislab allocator, false otherwise
 */
bool ptr_comes_from_minislab(void* ptr) {
	// Check if the pointer is in the minislab memory region
	size_t minislab_begin = (size_t)&challoc_minislab;
	size_t minislab_end   = minislab_begin + sizeof(MiniSlab);
	size_t ptr_addr	      = (size_t)ptr;
	return ptr_addr >= minislab_begin && ptr_addr < minislab_end;
}

/**
 * @brief Get the size of a pointer allocated from the minislab allocator
 * @param ptr The pointer to check
 * @return The size of the allocation
 */
size_t minislab_ptr_size(void* ptr) {
	assert(ptr_comes_from_minislab(ptr));
	ssize_t offset = (ssize_t)ptr - (ssize_t)&challoc_minislab;
	assert(offset < 4096);

	static const int sizes[]	 = {512, 256, 128, 64, 32, 16, 8, 4};		    // Sizes of the layers
	static const int layer_offsets[] = {512, 1024, 1536, 2048, 2560, 3072, 3584, 4096}; // Offsets of the layers

	// Find the layer of the pointer
	if (offset >= 0 && offset < layer_offsets[0]) {
		return 512;
	}
	for (int i = 1; i < 8; i++) {
		if (offset >= layer_offsets[i - 1] && offset < layer_offsets[i]) {
			return sizes[i];
		}
	}

	return SIZE_MAX; // Should not reach here
}

/**
 * @brief Free a pointer allocated from the minislab allocator
 * @param ptr The pointer to free
 */
void minislab_free(void* ptr) {
	assert(ptr_comes_from_minislab(ptr));
	ssize_t offset = (ssize_t)ptr - (ssize_t)&challoc_minislab;
	assert(offset < 4096);

	// Set the appropriate bitmask to 0 to signify it can be used again
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
			assert(false); // Should not reach here
	}
}
/** @} */

/// ------------------------------------------------
/// Fragmented Block Allocator
/// ------------------------------------------------

/** \defgroup Challoc_block Fragmented Block Allocator
 *  @{
 */

/**
 * @brief Double linked list metadata for the fragmented block allocator
 */
typedef struct AllocMetadata AllocMetadata;
struct AllocMetadata {
	size_t size;	     ///< Size of the allocated memory
	AllocMetadata* next; ///< Next block in the linked list
	AllocMetadata* prev; ///< Previous block in the linked list
	size_t block_idx;    ///< In which block the memory is allocated
};

/**
 * @brief Print the metadata of an allocation
 * @param metadata The metadata to print
 */
void allocmetadata_print(AllocMetadata* metadata) {
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

/**
 * @brief Get the metadata of an allocation from the pointer
 * @param ptr The pointer to the allocated memory
 * @return The metadata of the allocation
 */
AllocMetadata* challoc_get_metadata(void* ptr) {
	return (AllocMetadata*)(ptr - sizeof(AllocMetadata));
}

/**
 * @brief Structure to represent a block of memory allocated with mmap
 */
typedef struct {
	size_t size;		///< Size of the mmap block
	size_t free_space;	///< Space left in the mmap block
	AllocMetadata* head;	///< Head of the linked list
	AllocMetadata* tail;	///< Tail of the linked list
	void* mmap_ptr;		///< Pointer to the mmap block
	uint8_t time_to_live;	///< Number of mallocs in which this block wasn't reused before it is truly unmapped
	bool freshly_allocated; ///< True if the block was just allocated (and therefore full of 0)
} Block;

/**
 * @brief List of blocks as a dynamic array
 */
typedef struct {
	Block* blocks;	 ///< Array of blocks
	size_t size;	 ///< Number of blocks in the array
	size_t capacity; ///< Capacity of the array
} BlockList;

/**
 * @brief Get the time to live of a block based on its size
 * @param size The size of the block
 * @return The time to live of the block
 */
size_t ceil_to_4096multiple(size_t size) {
	size_t remainder = size % 4096;
	if (remainder == 0) {
		return size;
	}
	return size + 4096 - remainder;
}

/**
 * @brief Get the time to live of a block based on its size, the bigger the block, the less time we keep it alive
 * @param size The size of the block
 * @return The time to live of the block
 */
size_t time_to_live_with_size(size_t size) {
	size_t ceil = ceil_to_4096multiple(size);
	size_t log2 = 4096; // size of a page
	size_t ttl  = 5;    // 5 is the max time to live

	// Compute the log2 of the size
	while (log2 < ceil) {
		log2 *= 2;
		ttl--;
		if (ttl == 1) {
			break;
		}
	}

	return ttl;
}

/**
 * @brief Create a new block list with a given capacity
 * @param capacity The initial capacity of the block list
 * @return The newly created block list
 */
BlockList blocklist_with_capacity(size_t capacity) {
	return (BlockList){
	    .blocks   = mmap(NULL, capacity * sizeof(Block), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
	    .size     = 0,
	    .capacity = capacity,
	};
}

/**
 * @brief Destroy a block list
 * @param list The block list to destroy
 */
void blocklist_destroy(BlockList* list) {
	munmap(list->blocks, list->capacity * sizeof(Block));
}

/**
 * @brief Allocate a new block in the block list
 * @param list The block list
 * @param size_requested The size requested for the block
 */
void blocklist_allocate_new_block(BlockList* list, size_t size_requested) {
	size_requested = ceil_to_4096multiple(size_requested);

	// Allocate the memory
	void* ptr = mmap(NULL, size_requested, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	// Add the block to the list
	if (list->size == list->capacity) {
		list->capacity *= 2;
		Block* new_blocks = mmap(NULL, list->capacity * sizeof(Block), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		memcpy(new_blocks, list->blocks, list->size * sizeof(Block));
		munmap(list->blocks, list->size * sizeof(Block));
		list->blocks = new_blocks;
	}

	// Initialize the block
	list->blocks[list->size] = (Block){
	    .size	       = size_requested,
	    .free_space	       = size_requested,
	    .head	       = NULL,
	    .tail	       = NULL,
	    .freshly_allocated = true,
	    .mmap_ptr	       = ptr,
	};

	// Increment the size of the list
	list->size++;
}

BlockList challoc_blocks_in_use = {0}; ///< List of blocks in use
BlockList challoc_freed_blocks	= {0}; ///< List of freed blocks

/**
 * @brief Print a block
 * @param block The block to print
 */
void block_print(Block* block) {
	printf("Block n°%zu (%zu / %zu bytes) : ", block - challoc_blocks_in_use.blocks, block->free_space, block->size);
	for (AllocMetadata* current = block->head; current != NULL; current = current->next) {
		assert(current->block_idx == (size_t)(block - challoc_blocks_in_use.blocks));
		printf("[[%zu] %p | %zu] -> ", current->block_idx, current, current->size);
	}
	printf("x\n");
}

/**
 * @brief Print a block list
 * @param list The block list to print
 */
void blocklist_print(BlockList* list) {
	printf("BlockList (%zu / %zu) : \n", list->size, list->capacity);
	for (size_t i = 0; i < list->size; i++) {
		block_print(&list->blocks[i]);
	}
}

/**
 * @brief Push a block to the block list
 * @param list The block list
 * @param block The block to push
 */
void blocklist_push(BlockList* list, Block block) {
	if (list->size == list->capacity) {
		list->capacity *= 2;
		size_t size	  = list->capacity * sizeof(Block);
		size		  = ceil_to_4096multiple(size);
		Block* new_blocks = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		memcpy(new_blocks, list->blocks, list->size * sizeof(Block));
		if (munmap(list->blocks, list->size * sizeof(Block)) == -1) {
			perror("munmap");
		}
		list->blocks = new_blocks;
	}
	list->blocks[list->size] = block;
	list->size++;
}

/**
 * @brief Remove a block from the block list
 * @param list The block list
 * @param block_idx The index of the block to remove
 */
void blocklist_push_or_remove(BlockList* list, Block block) {
	if (list->size == list->capacity) {
		if (munmap(block.mmap_ptr, block.size) == -1) {
			perror("munmap");
		}
		return;
	}
	list->blocks[list->size] = block;
	list->size++;
}

/**
 * @brief Checks if a block has potentially enough free space to allocate a given size
 * @param block The block to check
 * @param size_requested The size requested
 * @return True if the block has potentially enough free space, false otherwise
 */
bool block_has_enough_space(Block* block, size_t size_requested) {
	return block->free_space >= size_requested + sizeof(AllocMetadata);
}

/**
 * @brief Peek a block from the block list
 * @param list The block list
 * @param block_idx The index of the block to peek
 * @return The block at the given index
 */
Block* blocklist_peek(BlockList* list, size_t block_idx) {
	assert(block_idx <= list->size);
	return &list->blocks[block_idx];
}

/**
 * @brief Tries to allocate within a block
 * @param list The block list
 * @param block_idx The index of the block to allocate from
 * @param size_requested The size requested
 * @return A pointer to the allocated memory, or NULL if the block is full
 */
void* block_try_allocate(BlockList* list, size_t block_idx, size_t size_requested) {
	assert(block_idx <= list->size);
	assert(list->blocks[block_idx].free_space <= list->blocks[block_idx].size);
	size_t size_needed = size_requested + sizeof(AllocMetadata); // We need to store the metadata

	// We will never be able to find a space
	Block* block = &list->blocks[block_idx];
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
			if (space_between_current_and_next >= (ssize_t)size_needed) { // We can allocate in between
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

	// We did not find a space
	return NULL;
}

/**
 * @brief Free an allocation from a block
 * @param block The block to free from
 * @param ptr The pointer to the allocated memory
 */
void block_free(Block* block, AllocMetadata* ptr) {
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

/**
 * @brief Free an allocation from a blocklist
 * @param ptr The pointer to the allocated memory
 */

void blocklist_free(AllocMetadata* ptr) {
	size_t block_idx = ptr->block_idx;
	Block* block	 = &challoc_blocks_in_use.blocks[block_idx];
	block_free(block, ptr);

	// Check if the block is empty
	if (block->free_space == block->size) {
		assert(block->head == NULL);

		// Push the block to the freed list
		blocklist_push_or_remove(&challoc_freed_blocks, *block);

		// Remove and swap the block from the allocated
		if (challoc_blocks_in_use.size == 1) {
			// The list is empty
			challoc_blocks_in_use.size = 0;
			return;
		}

		// Move the last block to the empty block
		Block* last_block			= &challoc_blocks_in_use.blocks[challoc_blocks_in_use.size - 1];
		challoc_blocks_in_use.blocks[block_idx] = *last_block;
		challoc_blocks_in_use.size--;
		for (AllocMetadata* current = block->head; current != NULL; current = current->next) {
			current->block_idx = block_idx;
		}
	}
}

/**
 * @brief Decrease the time to live of the blocks and unmap the ones which have reached 0.
 */
void decrease_ttl_and_unmap() {
	for (size_t i = 0; i < challoc_freed_blocks.size; i++) {
		assert(challoc_freed_blocks.blocks[i].head == NULL);
		Block* block = &challoc_freed_blocks.blocks[i];
		if (block->time_to_live == 1) {
			if (munmap(block->mmap_ptr, block->size) == -1) {
				perror("munmap");
			}
			// Move the last block to the empty block
			Block* last_block	       = &challoc_freed_blocks.blocks[challoc_freed_blocks.size - 1];
			challoc_freed_blocks.blocks[i] = *last_block;
			challoc_freed_blocks.size--;
			i--;
		}
		else {
			block->time_to_live--;
		}
	}
}
/** @} */

/// ------------------------------------------------
/// Challoc Internal API
/// ------------------------------------------------

/** \defgroup Challoc Challoc Internal API
 *  @{
 */

/**
 * @brief Allocates memory. Should never be called by the user directly.
 * @param size The size of the memory to allocate
 * @return A pointer to the allocated memory
 */
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
	for (size_t i = 0; i < challoc_blocks_in_use.size; i++) {
		Block* block = &challoc_blocks_in_use.blocks[i];
		if (block_has_enough_space(block, size)) {
			void* ptr = block_try_allocate(&challoc_blocks_in_use, i, size);
			if (ptr != NULL) {
				decrease_ttl_and_unmap();
				return ptr;
			}
		}
	}

	// Go through the list of freed blocks
	for (size_t i = 0; i < challoc_freed_blocks.size; i++) {
		Block block = challoc_freed_blocks.blocks[i];
		if (block_has_enough_space(&block, size)) {
			Block* last_block	       = &challoc_freed_blocks.blocks[challoc_freed_blocks.size - 1];
			challoc_freed_blocks.blocks[i] = *last_block;
			challoc_freed_blocks.size--;

			// Revive the block into a ready-to-use one
			block.time_to_live = time_to_live_with_size(block.size);
			block.head	   = NULL;
			block.tail	   = NULL;
			block.free_space   = block.size;
			blocklist_push(&challoc_blocks_in_use, block);
			void* ptr = block_try_allocate(&challoc_blocks_in_use, challoc_blocks_in_use.size - 1, size);
			decrease_ttl_and_unmap();
			return ptr;
		}
	}

	// No block had enough space, create a new one
	blocklist_allocate_new_block(&challoc_blocks_in_use, size + sizeof(AllocMetadata) + sizeof(Block));

	// Check if we could allocate the new one
	if (challoc_blocks_in_use.blocks == MAP_FAILED) {
		return NULL;
	}

	void* ptr = block_try_allocate(&challoc_blocks_in_use, challoc_blocks_in_use.size - 1, size);

	decrease_ttl_and_unmap();

	return ptr;
}

/**
 * @brief Free memory. Should never be called by the user directly. Assumes the pointer comes from challoc.
 * @param ptr The pointer to the memory to free
 */
void __chafree(void* ptr) {
	if (ptr == NULL) {
		return;
	}

	// Check if the pointer comes from the minislab before getting wrong metadata
	if (ptr_comes_from_minislab(ptr)) {
		minislab_free(ptr);
		return;
	}

	// Free the memory from the block list
	AllocMetadata* metadata = challoc_get_metadata(ptr);
	blocklist_free(metadata);
}

/**
 * @brief Allocate memory and set it to zero. Should never be called by the user directly.
 * @param size The size of the memory to allocate
 * @return A pointer to the allocated memory
 */
void* __chacalloc(size_t nmemb, size_t size) {
	// Allocate the memory
	void* ptr = __chamalloc(nmemb * size);

	// Check if the pointer comes from the minislab before getting wrong metadata
	if (ptr_comes_from_minislab(ptr)) {
		memset(ptr, 0, nmemb * size);
		return ptr;
	}

	// Check if it comes from a freshly allocated block
	AllocMetadata* metadata = challoc_get_metadata(ptr);
	size_t block_idx	= metadata->block_idx;
	Block* block		= &challoc_blocks_in_use.blocks[block_idx];
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

/**
 * @brief Reallocate memory. Should never be called by the user directly.
 * @param ptr The pointer to the memory to reallocate
 * @param new_size The new size of the memory
 * @return A pointer to the reallocated memory
 */
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
/** @} */

/// ------------------------------------------------
/// Challoc_leakcheck Challoc Leakcheck
/// ------------------------------------------------

/** \defgroup Challoc_leakcheck Challoc Leakcheck
 *  @{
 */

#ifdef CHALLOC_LEAKCHECK
/**
 * @brief Structure to trace memory allocations for leak checking
 */
typedef struct {
	void* ptr;   ///< Pointer to the allocated memory
	size_t size; ///< Size of the allocated memory
} AllocTrace;

/**
 * @brief Frees an allocation trace. Doesn't do anything for now because nothing needs to be stored dynamically.
 * @param trace The trace to free
 */
void alloctrace_free(AllocTrace trace) {}

/**
 * @brief Dynamic list of allocation traces
 */
typedef struct {
	AllocTrace* ptrs; ///< Array of allocation traces
	size_t size;	  ///< Number of allocation traces in the array
	size_t capacity;  ///< Capacity of the array
} LeakcheckTraceList;

/**
 * @brief Create a new leakcheck trace list with a given capacity
 * @param capacity The initial capacity of the trace list
 * @return The newly created trace list
 */
LeakcheckTraceList leakcheck_list_with_capacity(size_t capacity) {
	return (LeakcheckTraceList){
	    .ptrs     = mmap(NULL, capacity * sizeof(AllocTrace), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
	    .size     = 0,
	    .capacity = capacity,
	};
}

/**
 * @brief Push a pointer to the leakcheck trace list
 * @param list The trace list
 * @param ptr The pointer to push
 * @param ptr_size The size of the pointer
 */
void leakcheck_list_push(LeakcheckTraceList* list, void* ptr, size_t ptr_size) {
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

/**
 * @brief Remove a pointer from the leakcheck trace list
 * @param list The trace list
 * @param ptr The pointer to remove
 */
void leakcheck_list_remove_ptr(LeakcheckTraceList* list, void* ptr) {
	for (size_t i = 0; i < list->size; i++) {
		if (list->ptrs[i].ptr == ptr) {
			list->ptrs[i] = list->ptrs[list->size - 1];
			list->size--;
			alloctrace_free(list->ptrs[list->size]);
			return;
		}
	}
}

/**
 * @brief Free a leakcheck trace list
 * @param list The trace list to free
 */
void leakcheck_list_free(LeakcheckTraceList* list) {
	for (size_t i = 0; i < list->size; i++) {
		alloctrace_free(list->ptrs[i]);
	}
	munmap(list->ptrs, list->size * sizeof(AllocTrace));
}

LeakcheckTraceList challoc_leaktracker = {0}; ///< List of allocations for leak checking
#endif
/** @} */

void __attribute__((constructor)) init() {
	challoc_blocks_in_use = blocklist_with_capacity(30);
	challoc_freed_blocks  = blocklist_with_capacity(10);

#ifdef CHALLOC_LEAKCHECK
	challoc_leaktracker = leakcheck_list_with_capacity(10);
#endif
}

void __attribute__((destructor)) fini() {
#ifdef CHALLOC_LEAKCHECK
	if (challoc_leaktracker.size > 0) {
		fprintf(stderr, "challoc: detected %zu memory leaks\n", challoc_leaktracker.size);
		for (size_t i = 0; i < challoc_leaktracker.size; i++) {
			fprintf(stderr, "Leaked memory at %p, Content: ", challoc_leaktracker.ptrs[i].ptr);
			size_t ptr_size = challoc_leaktracker.ptrs[i].size;
			for (size_t j = 0; j < ptr_size; j++) {
				fprintf(stderr, "%02X ", ((uint8_t*)challoc_leaktracker.ptrs[i].ptr)[j]);
			}
			fprintf(stderr, "\n");
		}
		exit(EXIT_FAILURE);
	}

	printf("challoc: no memory leaks detected, congrats!\n");
	printf(" /\\_/\\\n>(^w^)<\n  b d\n");

	leakcheck_list_free(&challoc_leaktracker);
#endif
	blocklist_destroy(&challoc_blocks_in_use);
	blocklist_destroy(&challoc_freed_blocks);
}

/// ------------------------------------------------
/// Challoc Public API
/// ------------------------------------------------

/** \defgroup Challoc_pub Challoc Public API
 *  @{
 */

/**
 * @brief Allocate memory
 * @param size The size of the memory to allocate
 * @return A pointer to the allocated memory
 */
void* chamalloc(size_t size) {
	void* ptr;
	CHALLOC_MUTEX({
		ptr = __chamalloc(size);
#ifdef CHALLOC_LEAKCHECK
		leakcheck_list_push(&challoc_leaktracker, ptr, size);
#endif
	})
	return ptr;
}

/**
 * @brief Free memory
 * @param ptr The pointer to the memory to free
 */
void chafree(void* ptr) {
	CHALLOC_MUTEX({
		__chafree(ptr);
#ifdef CHALLOC_LEAKCHECK
		leakcheck_list_remove_ptr(&challoc_leaktracker, ptr);
#endif
	})
}

/**
 * @brief Allocate memory and set it to zero
 * @param nmemb The number of elements to allocate
 * @param size The size of each element
 * @return A pointer to the allocated memory
 */
void* chacalloc(size_t nmemb, size_t size) {
	void* ptr;
	CHALLOC_MUTEX({
		ptr = __chacalloc(nmemb, size);
#ifdef CHALLOC_LEAKCHECK
		leakcheck_list_push(&challoc_leaktracker, ptr, nmemb * size);
#endif
	})
	return ptr;
}

/**
 * @brief Reallocate memory
 * @param ptr The pointer to the memory to reallocate
 * @param size The new size of the memory
 * @return A pointer to the reallocated memory
 */
void* charealloc(void* ptr, size_t size) {
	void* new_ptr;
	CHALLOC_MUTEX({
		new_ptr = __charealloc(ptr, size);
#ifdef CHALLOC_LEAKCHECK
		leakcheck_list_remove_ptr(&challoc_leaktracker, ptr);
		leakcheck_list_push(&challoc_leaktracker, new_ptr, size);
#endif
	})
	return new_ptr;
}

/** @} */

// Set the interposing functions
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