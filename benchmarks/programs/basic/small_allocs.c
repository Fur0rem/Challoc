/**
 * @file benchmarks/programs/basic/small_allocs.c
 * @brief A program that does a lot of small allocations
 */

#include <stdlib.h>

int main() {
	const int NB_ELEMS = 50000;
	volatile int** ptr = malloc(NB_ELEMS * sizeof(int*));
	for (int i = 0; i < NB_ELEMS; i += 20) {
		ptr[i + 0]  = (volatile int*)malloc(895 + (i % 8) * sizeof(int));
		ptr[i + 1]  = (volatile int*)malloc(19 + (i % 8) * sizeof(int));
		ptr[i + 2]  = (volatile int*)malloc(84 + (i % 8) * sizeof(int));
		ptr[i + 3]  = (volatile int*)malloc(48 + (i % 8) * sizeof(int));
		ptr[i + 4]  = (volatile int*)malloc(97 + (i % 8) * sizeof(int));
		ptr[i + 5]  = (volatile int*)malloc(111 + (i % 8) * sizeof(int));
		ptr[i + 6]  = (volatile int*)malloc(355 + (i % 8) * sizeof(int));
		ptr[i + 7]  = (volatile int*)malloc(8 + (i % 8) * sizeof(int));
		ptr[i + 8]  = (volatile int*)malloc(95 + (i % 8) * sizeof(int));
		ptr[i + 9]  = (volatile int*)malloc(94 + (i % 8) * sizeof(int));
		ptr[i + 10] = (volatile int*)malloc(2 + (i % 8) * sizeof(int));
		ptr[i + 11] = (volatile int*)malloc(36 + (i % 8) * sizeof(int));
		ptr[i + 12] = (volatile int*)malloc(12 + (i % 8) * sizeof(int));
		ptr[i + 13] = (volatile int*)malloc(256 + (i % 8) * sizeof(int));
		ptr[i + 14] = (volatile int*)malloc(61 + (i % 8) * sizeof(int));
		ptr[i + 15] = (volatile int*)malloc(32 + (i % 8) * sizeof(int));
		ptr[i + 16] = (volatile int*)malloc(11 + (i % 8) * sizeof(int));
		ptr[i + 17] = (volatile int*)malloc(990 + (i % 8) * sizeof(int));
		ptr[i + 18] = (volatile int*)malloc(659 + (i % 8) * sizeof(int));
		ptr[i + 19] = (volatile int*)malloc(676 + (i % 8) * sizeof(int));
	}

	// Touch the memory to make sure it's allocated
	for (int i = 0; i < NB_ELEMS; i++) {
		*ptr[i] = 42;
	}

	for (int i = 0; i < NB_ELEMS; i++) {
		free(ptr[i]);
	}
	free(ptr);

	return 0;
}