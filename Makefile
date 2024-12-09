CC ?= gcc
LINK_DEV = -Ltarget -lchalloc_dev
EXEC_DEV = LD_LIBRARY_PATH=target
LINK_INTER = -Ltarget -lchalloc
EXEC_INTER = LD_LIBRARY_PATH=target LD_PRELOAD=libchalloc.so
PTHREAD = -lpthread

target:
	mkdir -p target

LEAKCHECK ?= false
ifeq ($(filter $(LEAKCHECK),true t 1),)
    ifeq ($(filter $(LEAKCHECK),false f 0),)
        $(error You have to set the LEAKCHECK variable to $(LEAKCHECK) but possible values are true or 1 for enabling leak check, 0 or false for disabling it)
    else
        $(eval LEAKCHECK_FLAG += -DNDCHALLOC_LEAKCHECK)
    endif
else
    $(eval LEAKCHECK_FLAG += -DCHALLOC_LEAKCHECK)
endif

libchalloc.so: src/challoc.c | target
	$(CC) -fpic -shared -O4 -Wall -Wextra -o target/libchalloc.so src/challoc.c $(LEAKCHECK_FLAG) -DCHALLOC_INTERPOSING $(PTHREAD)

libchalloc_dev.so: src/challoc.c | target
	$(CC) -fpic -shared -O4 -Wall -Wextra -o target/libchalloc_dev.so src/challoc.c $(LEAKCHECK_FLAG) -DNDCHALLOC_INTERPOSING $(PTHREAD)

challoc-dev: libchalloc_dev.so

challoc: libchalloc.so

check: challoc-dev tests/test.c tests/test_internal.c tests/** | target
	$(MAKE) libchalloc_dev.so LEAKCHECK=true
	$(CC) -o target/test_internal tests/test_internal.c $(LINK_DEV) -Wno-discarded-qualifiers -Og -g
	$(CC) -o target/test tests/test.c $(LINK_DEV) -Wno-discarded-qualifiers -Og -g $(PTHREAD)
	$(EXEC_DEV) target/test
	$(EXEC_DEV) target/test_internal
	$(CC) -o target/leaker_inter tests/programs/leaker_inter.c $(LINK_INTER) -Wno-discarded-qualifiers -Og -g
	$(CC) -o target/non_leaker_inter tests/programs/non_leaker_inter.c $(LINK_INTER) -Wno-discarded-qualifiers -Og -g
	$(MAKE) libchalloc.so LEAKCHECK=true
	!($(EXEC_INTER) target/leaker) || \
		(echo -e "\033[0;31mError: Leak check was expected to make this fail but it didn't\033[0m"; exit 1)
	$(EXEC_INTER) target/non_leaker
	$(MAKE) libchalloc_dev.so LEAKCHECK=true
	$(CC) -o target/leaker_non_inter tests/programs/leaker_non_inter.c $(LINK_DEV) -Wno-discarded-qualifiers -Og -g
	!($(EXEC_DEV) target/leaker_non_inter) || \
		(echo -e "\033[0;31mError: Leak check was expected to make this fail but it didn't\033[0m"; exit 1)
	$(CC) -o target/non_leaker_non_inter tests/programs/non_leaker_non_inter.c $(LINK_DEV) -Wno-discarded-qualifiers -Og -g
	$(EXEC_DEV) target/non_leaker_non_inter
	@echo -e "\033[0;32mAll tests passed\033[0m"

unit_benchmarks: benchmarks/run_unit_benchs.c challoc-dev | target
	$(CC) -o target/run_unit_benchs benchmarks/run_unit_benchs.c $(LINK_DEV) -Wno-discarded-qualifiers

program_benchmarks: benchmarks/run_program_benchs.c challoc | target
	$(CC) -o target/run_program_benchs benchmarks/run_program_benchs.c -Wno-discarded-qualifiers

benchmarks: libchalloc_dev.so libchalloc.so unit_benchmarks program_benchmarks | target
	$(if $(LABEL),,$(error Please provide a name for a directory to store the benchmarks and figures with LABEL=<...>))
	$(if $(wildcard benchmarks/results/$(LABEL)), $(error Directory benchmarks/results/$(LABEL) already exists. Don't want to overwrite.),)
	mkdir -p benchmarks/results/$(LABEL)/{unit,program}
	$(EXEC_DEV) target/run_unit_benchs $(LABEL)/unit
	LD_LIBRARY_PATH=target target/run_program_benchs $(LABEL)/program
	mkdir -p rapport/bench_results/$(LABEL)
	python3 benchmarks/plot_benchmarks.py benchmarks/results/$(LABEL) rapport/bench_results/$(LABEL)

clean:
	rm -rf target

rapport: rapport/*.typ rapport/images/* rapport/bench_results/** | target
	typst compile rapport/rapport.typ rapport/rapport.pdf