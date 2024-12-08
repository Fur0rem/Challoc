#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#undef INTERPOSING
#include "../src/challoc.h"

bool test_malloc() {
	for (size_t size = 1; size < 10000; size++) {
		volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
		;
		if (ptr == NULL) {
			return false;
		}
		chafree(ptr);
	}
	return true;
}

bool test_mallocs_dont_overlap() {
	const size_t NB_PTRS = 1000;
	volatile uint8_t* ptrs[NB_PTRS];
	for (size_t i = 0; i < NB_PTRS; i++) {
		ptrs[i] = chamalloc(i);
		if (ptrs[i] == NULL) {
			return false;
		}
	}
	for (size_t i = 0; i < NB_PTRS; i++) {
		for (size_t j = 0; j < i; j++) {
			volatile uint8_t* ptr_i = ptrs[i];
			volatile uint8_t* ptr_j = ptrs[j];
			size_t end_i			= (size_t)ptr_i + i;
			size_t end_j			= (size_t)ptr_j + j;
			if (end_i > (size_t)ptr_j && end_j > (size_t)ptr_i) {
				return false;
			}
		}
	}
	for (size_t i = 0; i < NB_PTRS; i++) {
		chafree(ptrs[i]);
	}
	return true;
}

bool test_calloc() {
	for (size_t size = 1; size < 10000; size++) {
		volatile uint8_t* ptr = chacalloc(1, size);
		if (ptr == NULL) {
			return false;
		}
		for (size_t i = 0; i < size; i++) {
			if (ptr[i] != 0) {
				return false;
			}
		}
		chafree(ptr);
	}
	return true;
}

bool test_realloc() {
	size_t size			  = 1024;
	volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
	if (ptr == NULL) {
		return false;
	}
	for (size_t i = 0; i < size; i++) {
		ptr[i] = 10;
	}
	size_t new_size			  = 2048;
	volatile uint8_t* new_ptr = charealloc(ptr, new_size * sizeof(uint8_t));
	if (new_ptr == NULL) {
		return false;
	}
	for (size_t i = 0; i < size; i++) {
		if (new_ptr[i] != 10) {
			return false;
		}
	}
	for (size_t i = size; i < new_size; i++) {
		new_ptr[i] = 20;
	}
	chafree(new_ptr);
	return true;
}

typedef struct {
	const char* name;
	bool (*test)();
} Test;

#define TEST(name)                                                                                                                         \
	{ #name, name }

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
	bool all_passed = true;

	for (size_t i = 0; i < sizeof(tests) / sizeof(Test); i++) {
		Test test	= tests[i];
		bool passed = test.test();

		printf("[Running] %s\n", test.name);
		// go back to the beginning of the line and clear it
		printf("\033[1A\033[2K");

		if (passed) {
			printf(GREEN_BOLD "[  OK  ]" RESET " %s\n", test.name);
		}
		else {
			printf(RED_BOLD "[FAILED]" RESET " %s\n", test.name);
		}
		all_passed &= passed;
	}

	if (all_passed) {
		printf(GREEN_BOLD "All tests passed\n" RESET);
		exit(EXIT_SUCCESS);
	}
	else {
		printf(RED_BOLD "Some tests failed\n" RESET);
		exit(EXIT_FAILURE);
	}
}