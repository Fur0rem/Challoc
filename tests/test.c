#include <errno.h>
#include <error.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#undef INTERPOSING
#include "../src/challoc.h"

bool test_malloc() {
	for (size_t size = 1; size < 10000; size++) {
		// printf("size: %zu\n", size);
		volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
		if (ptr == NULL) {
			return false;
		}
		chafree(ptr);
	}
	return true;
}

bool test_mallocs_dont_overlap() {
	const size_t NB_PTRS	= 1000;
	volatile uint8_t** ptrs = malloc(NB_PTRS * sizeof(uint8_t*));
	printf("Allocating memory\n");
	for (size_t i = 1; i < NB_PTRS; i++) {
		ptrs[i] = chamalloc(i * sizeof(uint8_t));
		if (ptrs[i] == NULL) {
			return false;
		}
	}
	printf("Checking for overlaps\n");
	bool overlap = false;
	for (size_t i = 1; i < NB_PTRS; i++) {
		for (size_t j = 1; j < i; j++) {
			volatile uint8_t* ptr_i = ptrs[i];
			volatile uint8_t* ptr_j = ptrs[j];
			size_t end_i		= (size_t)ptr_i + (i * sizeof(uint8_t));
			size_t end_j		= (size_t)ptr_j + (j * sizeof(uint8_t));
			if (end_i > (size_t)ptr_j && end_j > (size_t)ptr_i) {
				fprintf(stderr,
					"Memory regions overlap: [%p-%p] and [%p-%p] for i=%zu and j=%zu\n",
					ptr_i,
					(void*)end_i,
					ptr_j,
					(void*)end_j,
					i,
					j);
				overlap = true;
			}
		}
	}
	printf("Freeing memory\n\n");
	for (size_t i = 0; i < NB_PTRS; i++) {
		chafree(ptrs[i]);
	}
	free(ptrs);
	return !overlap;
}

bool test_calloc() {
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

bool test_realloc() {
	for (size_t size = 1; size <= 4096; size *= 2) {
		volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
		if (ptr == NULL) {
			printf("chamalloc failed for size %zu\n", size);
			return false;
		}
		for (size_t i = 0; i < size; i++) {
			ptr[i] = 10;
		}

		for (size_t i = 0; i < size; i++) {
			if (ptr[i] != 10) {
				printf("%p : Data mismatch before realloc at index %zu, expected 10, got %d\n", ptr, i, ptr[i]);
				chafree(ptr);
				return false;
			}
		}

		size_t new_size		  = size * 2;
		volatile uint8_t* new_ptr = charealloc(ptr, new_size * sizeof(uint8_t));
		printf("old size %zu, new size %zu, old ptr %p, new ptr %p\n", size, new_size, ptr, new_ptr);
		if (new_ptr == NULL) {
			chafree((void*)ptr);
			printf("charealloc failed for new size %zu\n", new_size);
			return false;
		}
		for (size_t i = size; i < new_size; i++) {
			new_ptr[i] = 20;
		}

		for (size_t i = 0; i < size; i++) {
			if (new_ptr[i] != 10) {
				printf("%p : Data mismatch after realloc at index %zu, expected 10, got %d\n", new_ptr, i, new_ptr[i]);
				chafree((void*)new_ptr);
				return false;
			}
		}
		for (size_t i = size; i < new_size; i++) {
			if (new_ptr[i] != 20) {
				printf("%p : Data mismatch after++ realloc at index %zu, expected 20, got %d\n", new_ptr, i, new_ptr[i]);
				chafree((void*)new_ptr);
				return false;
			}
		}

		chafree(new_ptr);
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
    TEST(test_malloc),
    TEST(test_mallocs_dont_overlap),
    TEST(test_calloc),
    TEST(test_realloc),
};

int main() {
	errno		= 0;
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