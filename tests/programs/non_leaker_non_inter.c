/**
 * @file tests/programs/non_leaker_non_inter.c
 * @brief A program that doesn't leak memory and doesn't the interposing feature
 */

#include "../../src/challoc.h"
#include <stdint.h>
#include <stdlib.h>

void* fn_that_allocs() {
	for (int i = 0; i < 20; i++) {
		volatile uint16_t* ptr = chamalloc(sizeof(uint16_t));
		*ptr		       = 0xFECA;
		chafree(ptr);
	}
}

void fn_that_forgets_to_free() {
	fn_that_allocs();
}

int main() {
	fn_that_forgets_to_free();
	exit(EXIT_SUCCESS);
}