#include <stdlib.h>

int main() {
	const int NB_ELEMS = 20000;
	volatile int** ptr = malloc(NB_ELEMS * sizeof(int*));
	for (int i = 0; i < NB_ELEMS; i += 20) {
		// Alternate small and big allocations
		ptr[i + 0]  = (volatile int*)malloc(895 + (i % 8));
		ptr[i + 1]  = (volatile int*)malloc(121259 + i * 2);
		ptr[i + 2]  = (volatile int*)malloc(84 + (i % 8));
		ptr[i + 3]  = (volatile int*)malloc(48648 + i * 2);
		ptr[i + 4]  = (volatile int*)malloc(13511 + i * 2);
		ptr[i + 5]  = (volatile int*)malloc(97 + (i % 8));
		ptr[i + 6]  = (volatile int*)malloc(355 + (i % 8));
		ptr[i + 7]  = (volatile int*)malloc(308937 + i * 2);
		ptr[i + 8]  = (volatile int*)malloc(11124 + (i % 8));
		ptr[i + 9]  = (volatile int*)malloc(2 + (i % 8));
		ptr[i + 10] = (volatile int*)malloc(12 + (i % 8));
		ptr[i + 11] = (volatile int*)malloc(396820 + i * 2);
		ptr[i + 12] = (volatile int*)malloc(933914 + i * 2);
		ptr[i + 13] = (volatile int*)malloc(201256 + i * 2);
		ptr[i + 14] = (volatile int*)malloc(61 + (i % 8));
		ptr[i + 15] = (volatile int*)malloc(628276 + i * 2);
		ptr[i + 16] = (volatile int*)malloc(11 + (i % 8));
		ptr[i + 17] = (volatile int*)malloc(9622990 + i * 2);
		ptr[i + 18] = (volatile int*)malloc(659 + (i % 8));
		ptr[i + 19] = (volatile int*)malloc(32295 + i * 2);
	}

	// Touch the memory to make sure it's allocated
	for (int i = 0; i < NB_ELEMS; i++) {
		ptr[i][0] = 42;
	}

	for (int i = 0; i < NB_ELEMS; i++) {
		free(ptr[i]);
	}
	free(ptr);

	return 0;
}