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
    TEST(test_minislab_fits_page),
    TEST(test_minislab_malloc),
};

int main() {
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