#include <stdlib.h>

int main() {
	const int SIZE_X    = 10;
	const int SIZE_Y    = 100000;
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