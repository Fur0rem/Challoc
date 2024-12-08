#include <dirent.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// rdtsc
#ifdef _MSC_VER
#	include <intrin.h>
#else
#	include <x86intrin.h>
#endif

#define BLUE  "\033[34m"
#define RESET "\033[0m"
#define BOLD  "\033[1m"

uint64_t cpu_freq = 0;

// Hacky way to get a rough estimate of the CPU frequency
uint64_t cpu_frequency() {
	const int NB_SECS = 3;
	uint64_t start	  = __rdtsc();
	sleep(NB_SECS);
	uint64_t end = __rdtsc();
	return (end - start) / NB_SECS;
}

typedef enum {
	LIBC,
	CHALLOC,
} Allocator;

typedef struct {
	Allocator allocator;
	char* fn_name;
	size_t nb_iters;
	uint64_t* times;
	size_t memory_usage;
} BenchResult;

#define MAX_PATH    1024
#define MAX_COMMAND 2048

void recursively_list_all_files(const char* dir_name, char files[][MAX_PATH], int* count) {
	DIR* dir = opendir(dir_name);
	printf("Opening directory: %s\n", dir_name);
	if (!dir) {
		perror("opendir");
		return;
	}

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			char path[MAX_PATH];
			snprintf(path, sizeof(path), "%s/%s", dir_name, entry->d_name);
			recursively_list_all_files(path, files, count);
		}
		else {
			if (strstr(entry->d_name, ".c")) {
				snprintf(files[*count], MAX_PATH, "%s/%s", dir_name, entry->d_name);
				(*count)++;
			}
		}
	}
	closedir(dir);
}

void compile_benchmark(const char* source_file, const char* output_file, const char* flags) {
	char command[MAX_COMMAND];
	snprintf(command, sizeof(command), "gcc %s %s -o %s -Wno-discarded-qualifiers", source_file, flags, output_file);
	int ret = system(command);
	if (ret != 0) {
		fprintf(stderr, "Compilation failed for %s\n", source_file);
		printf("Command: %s\n", command);
		exit(EXIT_FAILURE);
	}
}

void run_benchmark(const char* command, BenchResult* result, char* command_preload) {
	const uint64_t target_cycles = cpu_freq / 2; // half a second

	char full_command[300];
	snprintf(full_command, 300, "%s %s\n", command_preload, command);
	// printf("Running command: %s\n", full_command);

	const int SECS_TO_NS = (uint64_t)1e9;

	uint64_t rough_estimate_total	  = 0;
	const int NB_ITERS_FOR_ESTIMATION = 1;
	for (int i = 0; i < NB_ITERS_FOR_ESTIMATION; i++) {
		struct timespec bench_start, bench_end;
		clock_gettime(CLOCK_MONOTONIC, &bench_start);

		int ret = system(full_command);
		if (ret != 0) {
			fprintf(stderr, "Execution failed for %s\n", full_command);
			exit(EXIT_FAILURE);
		}

		clock_gettime(CLOCK_MONOTONIC, &bench_end);
		uint64_t elapsed_ns = (bench_end.tv_sec - bench_start.tv_sec) * SECS_TO_NS + (bench_end.tv_nsec - bench_start.tv_nsec);
		rough_estimate_total += elapsed_ns;
	}
	double rough_estimate = (double)rough_estimate_total / (double)NB_ITERS_FOR_ESTIMATION;

	int nb_iters	    = target_cycles / rough_estimate;
	const int MAX_ITERS = 100000;
	const int MIN_ITERS = 5;
	nb_iters	    = nb_iters > MAX_ITERS ? MAX_ITERS : nb_iters;
	nb_iters	    = nb_iters < MIN_ITERS ? MIN_ITERS : nb_iters;

	printf("Will do" BOLD " %d " RESET "iterations\n", nb_iters);
	result->times	 = malloc(nb_iters * sizeof(double));
	result->nb_iters = nb_iters;
	for (int n = 0; n < nb_iters; n++) {
		struct timespec bench_start, bench_end;
		clock_gettime(CLOCK_MONOTONIC, &bench_start);
		int ret = system(full_command);
		if (ret != 0) {
			fprintf(stderr, "Execution failed for %s\n", full_command);
			exit(EXIT_FAILURE);
		}
		clock_gettime(CLOCK_MONOTONIC, &bench_end);
		result->times[n] = (bench_end.tv_sec - bench_start.tv_sec) * SECS_TO_NS + (bench_end.tv_nsec - bench_start.tv_nsec);
	}

	char memory_command[MAX_COMMAND];
	snprintf(memory_command, sizeof(memory_command), "time -f '%%M' sh -c '%s' 2>&1", full_command);
	// printf("Memory command: %s\n", memory_command);
	FILE* memory_pipe = popen(memory_command, "r");
	if (!memory_pipe) {
		perror("popen");
		exit(EXIT_FAILURE);
	}
	char memory_output[300];
	char* res = fgets(memory_output, sizeof(memory_output), memory_pipe);
	if (!res) {
		perror("fgets");
		exit(EXIT_FAILURE);
	}
	const size_t KB = 1024;
	sscanf(memory_output, "%zu", &result->memory_usage);
	result->memory_usage *= KB;

	pclose(memory_pipe);

	// Go back to line
	printf("\033[A");
	printf("\033[K");
}

char* clone_and_remove_extension(char* filename) {
	char* filename2 = strdup(filename);
	char* ext_idx	= strrchr(filename2, '.');
	*ext_idx	= '\0';
	return filename2;
}

void write_results(BenchResult libc, BenchResult challoc, char* output_dir) {
	// Write it in csv (size, libc, challoc)
	char full_path[200];
	snprintf(full_path, 200, "benchmarks/results/%s/%s.json", output_dir, libc.fn_name);
	// printf("Writing results to %s\n", full_path);
	FILE* file = fopen(full_path, "w");
	if (!file) {
		perror("fopen");
	}

	fprintf(file, "{\n");
	fprintf(file, "\t\"libc\": {\n");
	// write an array of times
	fprintf(file, "\t\t\"times\": [");
	for (int i = 0; i < libc.nb_iters; i++) {
		fprintf(file, "%zu", libc.times[i]);
		if (i != libc.nb_iters - 1) {
			fprintf(file, ",");
		}
	}
	fprintf(file, "],\n");
	fprintf(file, "\t\t\"memory\": %zu\n", libc.memory_usage);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"challoc\": {\n");
	// write an array of times
	fprintf(file, "\t\t\"times\": [");
	for (int i = 0; i < challoc.nb_iters; i++) {
		fprintf(file, "%zu", challoc.times[i]);
		if (i != challoc.nb_iters - 1) {
			fprintf(file, ",");
		}
	}
	fprintf(file, "],\n");
	fprintf(file, "\t\t\"memory\": %zu\n", challoc.memory_usage);
	fprintf(file, "\t}\n");
	fprintf(file, "}\n");

	fclose(file);
}

void free_results(BenchResult* result) {
	free(result->times);
}

int main(int argc, char** argv) {

	printf("Estimating CPU frequency\n");
	const int NB_FREQ_SAMPLES = 1;
	for (int i = 0; i < NB_FREQ_SAMPLES; i++) {
		cpu_freq += cpu_frequency();
	}
	cpu_freq = cpu_freq / NB_FREQ_SAMPLES;

	// Arguments: the directory to store the results in
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <output_dir>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char files[100][MAX_PATH];
	int count = 0;

	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != NULL) {
		printf("Current working directory: %s\n", cwd);
	}
	else {
		perror("getcwd() error");
	}
	recursively_list_all_files("benchmarks/programs", files, &count);
	int res = system("make challoc");
	if (res != 0) {
		fprintf(stderr, "Compilation of challoc failed\n");
		exit(EXIT_FAILURE);
	}
	mkdir("target/c_bins", 0755);

	// Sort files alphabetically
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count; j++) {
			if (strcmp(files[i], files[j]) > 0) {
				char tmp[MAX_PATH];
				strcpy(tmp, files[i]);
				strcpy(files[i], files[j]);
				strcpy(files[j], tmp);
			}
		}
	}

	for (int i = 0; i < count; i++) {
		char output[MAX_PATH];
		char* without_ext = clone_and_remove_extension(files[i]);
		snprintf(output, sizeof(output), "target/c_bins/%s", basename(without_ext));
		compile_benchmark(files[i], output, "-O3");

		char command[MAX_COMMAND];
		snprintf(command, sizeof(command), "./%s", output);

		BenchResult libc_result = {
		    .allocator = LIBC,
		    .fn_name   = basename(without_ext),
		};

		BenchResult challoc_result = {
		    .allocator = CHALLOC,
		    .fn_name   = basename(without_ext),
		};

		printf("Benchmarking " BLUE "%s\n" RESET, libc_result.fn_name);

		printf(BOLD "libc : " RESET);
		run_benchmark(command, &libc_result, "");
		double libc_total_time = 0;
		for (int n = 0; n < libc_result.nb_iters; n++) {
			libc_total_time += libc_result.times[n];
		}
		double libc_avg_time = libc_total_time / libc_result.nb_iters;
		printf("libc: %.9f seconds, %zu bytes\n", libc_avg_time * 1e-9, libc_result.memory_usage);

		printf(BOLD "challoc : " RESET);
		run_benchmark(command, &challoc_result, "LD_LIBRARY_PATH=target/ LD_PRELOAD=target/libchalloc.so");
		double challoc_total_time = 0;
		for (int n = 0; n < challoc_result.nb_iters; n++) {
			challoc_total_time += challoc_result.times[n];
		}
		double challoc_avg_time = challoc_total_time / challoc_result.nb_iters;
		printf("challoc: %.9f seconds, %zu bytes\n", challoc_avg_time * 1e-9, challoc_result.memory_usage);

		write_results(libc_result, challoc_result, argv[1]);

		free_results(&libc_result);
		free_results(&challoc_result);
		free(without_ext);
	}

	return 0;
}