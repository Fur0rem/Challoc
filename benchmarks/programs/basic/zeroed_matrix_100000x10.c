/**
 * @file benchmarks/programs/basic/zeroed_matrix_100000x10.c
 * @brief A program that allocates a 100000x10 matrix (100000 rows of 10 elements) and sets it to zero
 */

#include <stdlib.h>

int main() {
	const int SIZE_X    = 100000;
	const int SIZE_Y    = 10;
	volatile int** ptrs = (volatile int**)malloc(SIZE_X * sizeof(int*));
	for (int i = 0; i < SIZE_X; i++) {
		ptrs[i] = (volatile int*)calloc(SIZE_Y, sizeof(int));
	}

	// Touch the memory to make sure it's allocated
	for (int i = 0; i < SIZE_X; i++) {
		volatile int x = ptrs[i][0];
	}

	for (int i = 0; i < SIZE_X; i++) {
		free(ptrs[i]);
	}
	free(ptrs);

	return 0;
}