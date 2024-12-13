/**
 * @file tests/programs/leaker_inter.c
 * @brief A program that leaks memory and uses the interposing feature
 */

#include <stdint.h>
#include <stdlib.h>

void* fn_that_allocs() {
	for (int i = 0; i < 20; i++) {
		volatile uint16_t* ptr = malloc(sizeof(uint16_t));
		*ptr		       = 0xFECA;
	}
}

void fn_that_forgets_to_free() {
	fn_that_allocs();
}

int main() {
	fn_that_forgets_to_free();
	exit(EXIT_SUCCESS);
}