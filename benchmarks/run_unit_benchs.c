/**
 * @file benchmarks/run_unit_benchs.c
 * @brief Compare the performance of challoc and libc of standalone function calls
 */

/** \addtogroup Challoc_prog_benchmarks Challoc Unit Benchmarks
 *  @{
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// For rdtsc
#ifdef _MSC_VER
#	include <intrin.h>
#else
#	include <x86intrin.h>
#endif

#include "../src/challoc.h"
#include <time.h>

#define BLUE  "\033[34m"
#define RESET "\033[0m"
#define BOLD  "\033[1m"

uint64_t cpu_freq = 0;

/**
 * @brief Warm up the clock by doing a lot of nops
 */
void warmup_clock() {
	printf("Warming up the clock with " BOLD "%lu" RESET " nop\n", cpu_freq);
	for (int i = 0; i < cpu_freq; i++) {
		asm volatile("nop");
	}
}

/**
 * @brief Touch the memory to make sure it's allocated
 * @param ptr The pointer to the memory
 * @param size_requested The size of the memory
 */
typedef enum {
	LIBC,	 ///< Use of the standard libc allocator
	CHALLOC, ///< Use of the challoc allocator
} Allocator;

#define MAX_SIZE 30 // 2^30 = 1 GB

/**
 * @brief The result of a benchmark
 */
typedef struct {
	Allocator allocator;	     ///< The allocator used
	char* fn_name;		     ///< The name of the function being benchmarked
	uint64_t time[MAX_SIZE + 1]; ///< The time taken for each size
} BenchResult;

/**
 * @brief Approximate the CPU frequency by measuring the number of cycles in a second
 * @return The number of cycles in a second
 */
uint64_t cpu_frequency() {
	const int NB_SECS = 3;
	uint64_t start	  = __rdtsc();
	sleep(NB_SECS);
	uint64_t end = __rdtsc();
	return (end - start) / NB_SECS;
}

/**
 * @brief Touch the memory to make sure it's allocated
 * @param ptr The pointer to the memory
 * @param size_requested The size of the ptr
 */
void touch_memory(volatile uint8_t* ptr, size_t size_requested) {
	// Touch the memory to make sure it's allocated
	size_t first	      = 0;
	size_t quarter	      = (size_requested - 1) / 4;
	size_t half	      = (size_requested - 1) / 2;
	size_t three_quarters = 3 * ((size_requested - 1) / 4);
	size_t last	      = size_requested - 1;
	ptr[first]	      = ~0;
	ptr[quarter]	      = ~0;
	ptr[half]	      = ~0;
	ptr[three_quarters]   = ~0;
	ptr[last]	      = ~0;
}

/**
 * @brief Benchmark the malloc function
 * @param allocator_result The result of the benchmark
 * @param alloc The allocator to use
 * @param dealloc The deallocator to use
 * @param fn_name The name of the function being benchmarked
 */
void bench_malloc(BenchResult* allocator_result, void* (*alloc)(size_t), void (*dealloc)(void*), char* fn_name) {
	const uint64_t target_cycles = cpu_freq / 2; // half a second
	printf("Benchmarking time set to " BOLD "%lu " RESET "cycles\n", target_cycles);
	for (uint64_t i = 0; i <= MAX_SIZE; i++) {
		size_t size_requested = 1ULL << i;
		printf(BLUE "%s(%zu):	" RESET, fn_name, size_requested);
		// Time the first one to estimate how many iterations we can do
		uint64_t start			  = __rdtsc();
		uint64_t end			  = start;
		const int NB_ITERS_FOR_ESTIMATION = 1;

		uint64_t worst_iter = 0;
		while (end <= start) {
			start = __rdtsc();
			for (int nb_iters = 0; nb_iters < NB_ITERS_FOR_ESTIMATION; nb_iters++) {
				volatile uint8_t* ptr = alloc(size_requested);
				// Touch the memory to make sure it's allocated
				touch_memory(ptr, size_requested);
				dealloc((void*)ptr);
			}
			end = __rdtsc();
		}

		int nb_iters	    = target_cycles * NB_ITERS_FOR_ESTIMATION / (end - start);
		const int MAX_ITERS = 100000;
		const int MIN_ITERS = 5;
		nb_iters	    = nb_iters > MAX_ITERS ? MAX_ITERS : nb_iters;
		nb_iters	    = nb_iters < MIN_ITERS ? MIN_ITERS : nb_iters;
		printf("Will do" BOLD " %d " RESET "iterations\n", nb_iters);

		struct timespec bench_start, bench_end;
		clock_gettime(CLOCK_MONOTONIC, &bench_start);
		for (int n = 0; n < nb_iters; n++) {
			volatile uint8_t* ptr = alloc(size_requested);
			touch_memory(ptr, size_requested);
			dealloc((void*)ptr);
		}
		clock_gettime(CLOCK_MONOTONIC, &bench_end);

		uint64_t elapsed_ns	  = (bench_end.tv_sec - bench_start.tv_sec) * 1e9 + (bench_end.tv_nsec - bench_start.tv_nsec);
		allocator_result->time[i] = elapsed_ns / nb_iters;
		// Go back to line
		printf("\033[A");
		printf("\033[K");
		printf("%s(%zu): average_time: %.9f seconds\n", fn_name, size_requested, (double)elapsed_ns / (double)nb_iters / 1e9);
	}
}

/**
 * @brief Benchmark the realloc function
 * @param allocator_result The result of the benchmark
 * @param alloc The allocator to use
 * @param realloc The reallocator to use
 * @param dealloc The deallocator to use
 * @param fn_name The name of the function being benchmarked
 */
void bench_realloc(BenchResult* allocator_result, void* (*alloc)(size_t), void* (*realloc)(void*, size_t), void (*dealloc)(void*),
		   char* fn_name) {
	const uint64_t target_cycles = cpu_freq / 2; // half a second
	printf("Benchmarking time set to " BOLD "%lu " RESET "cycles\n", target_cycles);
	for (uint64_t i = 0; i <= MAX_SIZE; i++) {
		size_t size_requested = 1ULL << i;
		printf(BLUE "%s(%zu):	" RESET, fn_name, size_requested);
		// Time the first one to estimate how many iterations we can do
		uint64_t start			  = __rdtsc();
		uint64_t end			  = start;
		const int NB_ITERS_FOR_ESTIMATION = 1;

		uint64_t worst_iter = 0;
		while (end <= start) {
			start = __rdtsc();
			for (int nb_iters = 0; nb_iters < NB_ITERS_FOR_ESTIMATION; nb_iters++) {
				volatile uint8_t* ptr = alloc(size_requested);
				// Touch the memory to make sure it's allocated
				touch_memory(ptr, size_requested);
				ptr = realloc(ptr, size_requested * 2);
				dealloc((void*)ptr);
			}
			end = __rdtsc();
		}

		int nb_iters	    = target_cycles * NB_ITERS_FOR_ESTIMATION / (end - start);
		const int MAX_ITERS = 100000;
		const int MIN_ITERS = 5;
		nb_iters	    = nb_iters > MAX_ITERS ? MAX_ITERS : nb_iters;
		nb_iters	    = nb_iters < MIN_ITERS ? MIN_ITERS : nb_iters;
		printf("Will do" BOLD " %d " RESET "iterations\n", nb_iters);

		struct timespec bench_start, bench_end;
		clock_gettime(CLOCK_MONOTONIC, &bench_start);
		for (int n = 0; n < nb_iters; n++) {
			volatile uint8_t* ptr = alloc(size_requested);
			touch_memory(ptr, size_requested);
			void* new_ptr = realloc((void*)ptr, size_requested * 2);
			dealloc(new_ptr);
		}
		clock_gettime(CLOCK_MONOTONIC, &bench_end);

		uint64_t elapsed_ns	  = (bench_end.tv_sec - bench_start.tv_sec) * 1e9 + (bench_end.tv_nsec - bench_start.tv_nsec);
		allocator_result->time[i] = elapsed_ns / nb_iters;
		// Go back to line
		printf("\033[A");
		printf("\033[K");
		printf("%s(%zu): average_time: %.9f seconds\n", fn_name, size_requested, (double)elapsed_ns / (double)nb_iters / 1e9);
	}
}

/**
 * @brief Benchmark the calloc function
 * @param allocator_result The result of the benchmark
 * @param calloc The allocator to use
 * @param dealloc The deallocator to use
 * @param fn_name The name of the function being benchmarked
 */
void bench_calloc(BenchResult* allocator_result, void* (*calloc)(size_t, size_t), void (*dealloc)(void*), char* fn_name) {
	const uint64_t target_cycles = cpu_freq; // 1 billion cycles
	printf("Benchmarking time set to " BOLD "%lu " RESET "cycles\n", target_cycles);
	for (uint64_t i = 0; i <= MAX_SIZE; i++) {
		size_t size_requested = 1ULL << i;
		printf(BLUE "%s(%zu):	" RESET, fn_name, size_requested);
		// Time the first one to estimate how many iterations we can do
		uint64_t start			  = __rdtsc();
		uint64_t end			  = start;
		const int NB_ITERS_FOR_ESTIMATION = 1;

		uint64_t worst_iter = 0;
		while (end <= start) {
			start = __rdtsc();
			for (int nb_iters = 0; nb_iters < NB_ITERS_FOR_ESTIMATION; nb_iters++) {
				volatile uint8_t* ptr = calloc(size_requested, 1);
				// Touch the memory to make sure it's allocated
				touch_memory(ptr, size_requested);
				dealloc((void*)ptr);
			}
			end = __rdtsc();
		}

		int nb_iters	    = target_cycles * NB_ITERS_FOR_ESTIMATION / (end - start);
		const int MAX_ITERS = 100000;
		const int MIN_ITERS = 5;
		nb_iters	    = nb_iters > MAX_ITERS ? MAX_ITERS : nb_iters;
		nb_iters	    = nb_iters < MIN_ITERS ? MIN_ITERS : nb_iters;
		printf("Will do" BOLD " %d " RESET "iterations\n", nb_iters);

		struct timespec bench_start, bench_end;
		clock_gettime(CLOCK_MONOTONIC, &bench_start);
		for (int n = 0; n < nb_iters; n++) {
			volatile uint8_t* ptr = calloc(size_requested, 1);
			touch_memory(ptr, size_requested);
			dealloc((void*)ptr);
		}
		clock_gettime(CLOCK_MONOTONIC, &bench_end);

		uint64_t elapsed_ns	  = (bench_end.tv_sec - bench_start.tv_sec) * 1e9 + (bench_end.tv_nsec - bench_start.tv_nsec);
		allocator_result->time[i] = elapsed_ns / nb_iters;
		// Go back to line
		printf("\033[A");
		printf("\033[K");
		printf("%s(%zu): average_time: %.9f seconds\n", fn_name, size_requested, (double)elapsed_ns / (double)nb_iters / 1e9);
	}
}

/**
 * @brief Write the results of a benchmark to a file
 * @param libc The result of the libc benchmark
 * @param challoc The result of the challoc benchmark
 * @param output_dir The output directory
 */
void write_results(BenchResult libc, BenchResult challoc, char* output_dir) {
	// Write it in csv (size, libc, challoc)
	char full_path[200];
	snprintf(full_path, 200, "benchmarks/results/%s/%s.csv", output_dir, libc.fn_name);
	printf("Writing results to %s\n", full_path);
	FILE* file = fopen(full_path, "w");
	if (!file) {
		perror("fopen");
	}

	fprintf(file, "size,libc,challoc\n");
	for (int i = 0; i <= MAX_SIZE; i++) {
		size_t size_requested = 1ULL << i;
		fprintf(file, "%zu,%lu,%lu\n", size_requested, libc.time[i], challoc.time[i]);
	}
	fclose(file);
}

int main(int argc, char** argv) {
	// Arguments: the directory to store the results in
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <output_dir>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	printf("Estimating CPU frequency for warm-up time\n");
	const int nb_freq_samples = 2;
	for (int i = 0; i < nb_freq_samples; i++) {
		cpu_freq += cpu_frequency();
	}
	cpu_freq = cpu_freq / nb_freq_samples;

	warmup_clock();

	BenchResult libc    = {LIBC, "malloc"};
	BenchResult challoc = {CHALLOC, "malloc"};
	bench_malloc(&libc, malloc, free, "malloc");
	bench_malloc(&challoc, chamalloc, chafree, "chamalloc");
	write_results(libc, challoc, argv[1]);

	libc.fn_name	= "realloc";
	challoc.fn_name = "charealloc";
	bench_realloc(&libc, malloc, realloc, free, "realloc");
	bench_realloc(&challoc, chamalloc, charealloc, chafree, "charealloc");
	write_results(libc, challoc, argv[1]);

	libc.fn_name	= "calloc";
	challoc.fn_name = "chacalloc";
	bench_calloc(&libc, calloc, free, "calloc");
	bench_calloc(&challoc, chacalloc, chafree, "chacalloc");
	write_results(libc, challoc, argv[1]);

	return 0;
}

/** @} */