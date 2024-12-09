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
		volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
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
		ptrs[i] = chamalloc(i * sizeof(uint8_t));
		if (ptrs[i] == NULL) {
			return false;
		}
	}
	bool overlap = false;
	for (size_t i = 0; i < NB_PTRS; i++) {
		for (size_t j = 0; j < i; j++) {
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
	for (size_t i = 0; i < NB_PTRS; i++) {
		chafree(ptrs[i]);
	}
	return !overlap;
}

bool test_calloc() {
	for (size_t size = 1; size < 10000; size++) {
		volatile uint8_t* ptr = chacalloc(size, sizeof(uint8_t));
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

// bool test_realloc() {
// 	for (size_t size = 1; size <= 4096; size *= 2) {
// 		volatile uint8_t* ptr = chamalloc(size * sizeof(uint8_t));
// 		if (ptr == NULL) {
// 			fprintf(stderr, "chamalloc failed for size %zu\n", size);
// 			return false;
// 		}
// 		for (size_t i = 0; i < size; i++) {
// 			ptr[i] = 10;
// 		}

// 		size_t new_size		  = 2048;
// 		volatile uint8_t* new_ptr = charealloc((void*)ptr, new_size * sizeof(uint8_t));
// 		if (new_ptr == NULL) {
// 			fprintf(stderr, "charealloc failed for new size %zu\n", new_size);
// 			chafree((void*)ptr);
// 			return false;
// 		}

// 		for (size_t i = 0; i < size; i++) {
// 			if (new_ptr[i] != 10) {
// 				fprintf(stderr, "Data mismatch after realloc at index %zu\n", i);
// 				chafree((void*)new_ptr);
// 				return false;
// 			}
// 		}

// 		for (size_t i = size; i < new_size; i++) {
// 			new_ptr[i] = 20;
// 		}

// 		for (size_t i = 0; i < size; i++) {
// 			if (new_ptr[i] != 10) {
// 				fprintf(stderr, "Data mismatch after setting new values at index %zu\n", i);
// 				chafree((void*)new_ptr);
// 				return false;
// 			}
// 		}

// 		for (size_t i = size; i < new_size; i++) {
// 			if (new_ptr[i] != 20) {
// 				fprintf(stderr, "New data mismatch at index %zu\n", i);
// 				chafree((void*)new_ptr);
// 				return false;
// 			}
// 		}

// 		chafree((void*)new_ptr);
// 	}
// 	return true;
// }

bool test_realloc() {
	for (size_t size = 1; size <= 4096; size *= 2) {
		volatile uint8_t* ptr = malloc(size * sizeof(uint8_t));
		if (ptr == NULL) {
			fprintf(stderr, "malloc failed for size %zu\n", size);
			return false;
		}
		for (size_t i = 0; i < size; i++) {
			ptr[i] = 10;
		}

		size_t new_size		  = 2048;
		volatile uint8_t* new_ptr = realloc((void*)ptr, new_size * sizeof(uint8_t));
		if (new_ptr == NULL) {
			fprintf(stderr, "realloc failed for new size %zu\n", new_size);
			free((void*)ptr);
			return false;
		}

		for (size_t i = 0; i < size; i++) {
			if (new_ptr[i] != 10) {
				fprintf(stderr, "Data mismatch after realloc at index %zu\n", i);
				free((void*)new_ptr);
				return false;
			}
		}

		for (size_t i = size; i < new_size; i++) {
			new_ptr[i] = 20;
		}

		for (size_t i = 0; i < size; i++) {
			if (new_ptr[i] != 10) {
				fprintf(stderr, "Data mismatch after setting new values at index %zu\n", i);
				free((void*)new_ptr);
				return false;
			}
		}

		for (size_t i = size; i < new_size; i++) {
			if (new_ptr[i] != 20) {
				fprintf(stderr, "New data mismatch at index %zu\n", i);
				free((void*)new_ptr);
				return false;
			}
		}

		free((void*)new_ptr);
	}
	return true;
}

bool test_multithreading() {
	// typedef void* (*thread_func)(void*);

	// pthread_t thread_id[8];
	// bool results[8] = {false};
	// pthread_create(&thread_id[0], NULL, (thread_func)test_malloc, NULL);
	// pthread_create(&thread_id[1], NULL, (thread_func)test_mallocs_dont_overlap, NULL);
	// pthread_create(&thread_id[2], NULL, (thread_func)test_calloc, NULL);
	// pthread_create(&thread_id[3], NULL, (thread_func)test_realloc, NULL);
	// pthread_create(&thread_id[4], NULL, (thread_func)test_malloc, NULL);
	// pthread_create(&thread_id[5], NULL, (thread_func)test_mallocs_dont_overlap, NULL);
	// pthread_create(&thread_id[6], NULL, (thread_func)test_calloc, NULL);
	// pthread_create(&thread_id[7], NULL, (thread_func)test_realloc, NULL);

	// for (size_t i = 0; i < 8; i++) {
	// 	pthread_join(thread_id[i], &results[i]);
	// }

	// for (size_t i = 0; i < 8; i++) {
	// 	if (!results[i]) {
	// 		return false;
	// 	}
	// }

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
    TEST(test_malloc), TEST(test_mallocs_dont_overlap), TEST(test_calloc), TEST(test_realloc),
    //     TEST(test_multithreading),
};

int main() {
	errno		= 0;
	bool all_passed = true;

	for (size_t i = 0; i < sizeof(tests) / sizeof(Test); i++) {
		Test test = tests[i];
		printf("[Running] %s\n", test.name);
		bool passed = test.test();

		// go back to the beginning of the line and clear it
		printf("\033[1A\033[2K");

		bool has_no_errors = errno == 0;
		if (passed && has_no_errors) {
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