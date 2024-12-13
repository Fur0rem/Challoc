#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#undef INTERPOSING
#include "../src/challoc.c"

bool test_minislab_fits_page() {
	if (sizeof(challoc_minislab) > 4096) {
		printf("minislab is too big : %zu\n", sizeof(challoc_minislab));
		return false;
	}
	return sizeof(challoc_minislab) <= 4096;
}

bool test_minislab_malloc() {
	for (size_t size = 4; size <= 512; size *= 2) {
		volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
		if (ptr == NULL) {
			printf("allocation with size %zu failed\n", size);
			return false;
		}
		if (!ptr_comes_from_minislab(ptr)) {
			printf("allocation with size %zu does not come from minislab\n", size);
			chafree(ptr);
			return false;
		}

		chafree(ptr);
	}

	volatile uint8_t* ptr = chamalloc(513 * sizeof(uint8_t));

	if (ptr == NULL) {
		printf("allocation with size 513 failed\n");
		return false;
	}
	if (ptr_comes_from_minislab(ptr)) {
		printf("allocation with size 513 comes from minislab\n");
		chafree(ptr);
		return false;
	}

	chafree(ptr);

	return true;
}

bool overlaps(volatile uint8_t* ptr1, size_t size1, volatile uint8_t* ptr2, size_t size2) {
	size_t end1 = (size_t)ptr1 + size1;
	size_t end2 = (size_t)ptr2 + size2;
	return end1 > (size_t)ptr2 && end2 > (size_t)ptr1;
}

bool any_overlaps(void** ptrs, size_t* sizes, size_t nb_ptrs) {
	for (size_t i = 0; i < nb_ptrs; i++) {
		for (size_t j = 0; j < i; j++) {
			if (overlaps(ptrs[i], sizes[i], ptrs[j], sizes[j])) {
				return true;
			}
		}
	}
	return false;
}

bool test_fill_minislab() {
	// Fill the minislab
	void* all_alloced[4096 * 2];
	size_t sizes[4096 * 2];
	size_t all_alloced_size = 0;
	size_t current_size	= 4;
	while (current_size <= 512) {
		bool comes_from_minislab = true;

		while (comes_from_minislab) {
			volatile uint8_t* ptr = chamalloc(current_size * sizeof(uint8_t));
			if (ptr == NULL) {
				printf("allocation with size %zu failed\n\n", current_size);
				return false;
			}
			comes_from_minislab	      = ptr_comes_from_minislab(ptr);
			all_alloced[all_alloced_size] = ptr;
			sizes[all_alloced_size]	      = current_size;
			all_alloced_size++;
		}
		MmapBlockList_print(&challoc_mmap_blocks);

		current_size *= 2;
	}

	// Check that there are no overlaps
	if (any_overlaps(all_alloced, sizes, all_alloced_size)) {
		printf("overlaps detected\n\n");
		for (size_t i = 0; i < all_alloced_size; i++) {
			chafree(all_alloced[i]);
		}
		return false;
	}

	// Check that the minislab is full
	for (size_t pow = 2; pow <= 512; pow *= 2) {
		volatile uint8_t* ptr = chamalloc(pow * sizeof(uint8_t));
		if (ptr_comes_from_minislab(ptr)) {
			printf("minislab is not full\n\n");
			MiniSlab_print_usage(challoc_minislab);
			chafree(ptr);
			return false;
		}
	}

	// Free all the blocks
	for (size_t i = 0; i < all_alloced_size; i++) {
		chafree(all_alloced[i]);
	}

	// Check that the minislab is empty
	void* ptr = chamalloc(4 * sizeof(uint8_t));
	if (!ptr_comes_from_minislab(ptr)) {
		printf("minislab is not empty\n\n");
		MiniSlab_print_usage(challoc_minislab);
		chafree(ptr);
		return false;
	}

	return true;
}

bool test_block_reusage() {
	// Allocate a block
	volatile uint8_t* ptr = chamalloc(1024 * sizeof(uint8_t));
	if (ptr == NULL) {
		printf("allocation with size 1024 failed\n");
		return false;
	}
	if (ptr_comes_from_minislab(ptr)) {
		printf("allocation with size 1024 comes from minislab\n");
		chafree(ptr);
		return false;
	}

	// Check that there is nothing in the freed list
	if (challoc_freed_blocks.size != 0) {
		printf("challoc_freed_blocks list has %zu elements, expected 0\n", challoc_freed_blocks.size);
		MmapBlock* block = &challoc_freed_blocks.blocks[0];
		MmapBlock_print(block);
		chafree(ptr);
		return false;
	}

	// Free the block
	chafree(ptr);

	// Check that there is one block in the freed list and not with a TTL of 0
	if (challoc_freed_blocks.size != 1) {
		printf("freed list has %zu elements, expected 1\n", challoc_freed_blocks.size);
		return false;
	}
	MmapBlock* block = MmapBlockList_peek(&challoc_freed_blocks, 0);
	if (block->time_to_live == 0) {
		printf("freed block has TTL of 0\n");
		return false;
	}

	return true;
}

bool test_unmap_a_block() {
	for (size_t size = 1; size < 10000; size++) {
		volatile uint8_t* ptr = chacalloc(size, sizeof(uint8_t));
		if (ptr == NULL) {
			printf("chacalloc failed for size %zu\n", size);
			return false;
		}
		for (size_t i = 0; i < size; i++) {
			if (ptr[i] != 0) {
				printf("Byte %zu is not 0 but %d\n", i, ptr[i]);
				chafree(ptr);
				return false;
			}
		}
		chafree(ptr);
	}
	return true;
}

typedef void* (*thread_func)(void*);

thread_func alloc_slab_thread(void* array_of_ptrs) {
	for (size_t i = 0; i < 1024; i++) {
		((void**)array_of_ptrs)[i] = chamalloc(4 * sizeof(uint8_t));
	}
	return NULL;
}

bool test_minislab_concurrent_usage() {
	// Launch 8 threads that allocate from the minislab
	pthread_t thread_id[8];
	void* array_of_ptrs[8][1024];
	for (size_t i = 0; i < 8; i++) {
		pthread_create(&thread_id[i], NULL, (thread_func)alloc_slab_thread, array_of_ptrs[i]);
	}

	for (size_t i = 0; i < 8; i++) {
		pthread_join(thread_id[i], NULL);
	}

	// Check that there are no overlaps
	for (size_t i = 0; i < 8; i++) {
		if (any_overlaps(array_of_ptrs[i], (size_t[1024]){4}, 1024)) {
			printf("overlaps detected in thread %zu\n", i);
			return false;
		}
	}

	// Free all the blocks
	for (size_t i = 0; i < 8; i++) {
		for (size_t j = 0; j < 1024; j++) {
			chafree(array_of_ptrs[i][j]);
		}
	}

	return true;
}

bool test_block_fragmentation() {
	void* ptr1 = chamalloc(1001);
	void* ptr2 = chamalloc(1002);
	void* ptr3 = chamalloc(1003);

	// MmapBlockList_print(&challoc_mmap_blocks);
	if (ptr_comes_from_minislab(ptr1)) {
		printf("ptr1 comes from minislab\n");
		return false;
	}
	if (ptr_comes_from_minislab(ptr2)) {
		printf("ptr2 comes from minislab\n");
		return false;
	}
	if (ptr_comes_from_minislab(ptr3)) {
		printf("ptr3 comes from minislab\n");
		return false;
	}

	AllocMetadata* metadata_ptr1 = challoc_get_metadata(ptr1);
	AllocMetadata* metadata_ptr2 = challoc_get_metadata(ptr2);
	AllocMetadata* metadata_ptr3 = challoc_get_metadata(ptr3);
	MmapBlock* block	     = MmapBlockList_peek(&challoc_mmap_blocks, metadata_ptr1->block_idx);

	if (block->head != metadata_ptr1) {
		printf("ptr1 should have been the head\n");
		MmapBlock_print(block);
		return false;
	}
	if (metadata_ptr1->next != metadata_ptr2) {
		printf("ptr2 should have the next of ptr1\n");
		MmapBlock_print(block);
		return false;
	}
	if (metadata_ptr2->next != metadata_ptr3) {
		printf("ptr2 should have the next of ptr1\n");
		MmapBlock_print(block);
		return false;
	}

	chafree(ptr1);

	if (block->head != metadata_ptr2) {
		printf("ptr2 should have been the head\n");
		MmapBlock_print(block);
		return false;
	}
	if (metadata_ptr2->next != metadata_ptr3) {
		printf("ptr2 should have the next of ptr1\n");
		MmapBlock_print(block);
		return false;
	}

	chafree(ptr3);

	if (block->head != metadata_ptr2) {
		printf("ptr2 should have been the head\n");
		MmapBlock_print(block);
		return false;
	}

	chafree(ptr2);

	if (block->head != NULL) {
		printf("head should be NULL\n");
		MmapBlock_print(block);
		return false;
	}

	return true;
}

typedef struct {
	const char* name;
	bool (*test)();
} Test;

#define TEST(name) {#name, name}

#define RED_BOLD   "\033[1;31m"
#define GREEN_BOLD "\033[1;32m"
#define RESET	   "\033[0m"

Test tests[] = {
    TEST(test_block_fragmentation),
    TEST(test_block_reusage),
    TEST(test_minislab_fits_page),
    TEST(test_minislab_malloc),
    TEST(test_fill_minislab),
    TEST(test_unmap_a_block),
    TEST(test_minislab_concurrent_usage),
};

int main() {
	bool all_passed = true;

	for (size_t i = 0; i < sizeof(tests) / sizeof(Test); i++) {
		Test test = tests[i];
		printf("[Running] %s\n", test.name);
		bool passed = test.test();

		bool has_no_errors = errno == 0;
		if (passed && has_no_errors) {
			// go back to the beginning of the line and clear it
			printf("\033[1A\033[2K");
			printf(GREEN_BOLD "[  OK  ]" RESET " %s\n", test.name);
		}
		else {
			printf(RED_BOLD "[FAILED]" RESET " %s", test.name);
			if (!has_no_errors) {
				printf(" | (errno: %d)", errno);
			}
			else {
				printf(" | Failed logic test");
			}
			printf("\n");
		}
		all_passed &= passed;
		all_passed &= has_no_errors;
	}

	if (all_passed) {
		printf(GREEN_BOLD "All unit tests passed\n" RESET);
		exit(EXIT_SUCCESS);
	}
	else {
		printf(RED_BOLD "Some unit tests failed\n" RESET);
		exit(EXIT_FAILURE);
	}
}