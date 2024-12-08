target:
	mkdir -p target

libchalloc.so: src/challoc.c | target
	gcc -fpic -shared -O4 -o target/libchalloc.so src/challoc.c -DCHALLOC_INTERPOSING

libchalloc_dev.so: src/challoc.c | target
	gcc -fpic -shared -O4 -o target/libchalloc_dev.so src/challoc.c

challoc-dev: libchalloc_dev.so

challoc: libchalloc.so

check: challoc-dev tests/test.c | target
	gcc -o target/test tests/test.c -Ltarget -lchalloc_dev -Wno-discarded-qualifiers
	LD_LIBRARY_PATH=target LD_PRELOAD=libchalloc_dev.so target/test

unit_benchmarks: benchmarks/run_unit_benchs.c challoc-dev | target
	gcc -o target/run_unit_benchs benchmarks/run_unit_benchs.c -Ltarget -lchalloc_dev -Wno-discarded-qualifiers

program_benchmarks: benchmarks/run_program_benchs.c challoc | target
	gcc -o target/run_program_benchs benchmarks/run_program_benchs.c -Wno-discarded-qualifiers

benchmarks: unit_benchmarks program_benchmarks | target
	$(if $(LABEL),,$(error Please provide a name for a directory to store the benchmarks and figures with LABEL=<...>))
	$(if $(wildcard benchmarks/results/$(LABEL)), $(error Directory benchmarks/results/$(LABEL) already exists. Don't want to overwrite.),)
	mkdir -p benchmarks/results/$(LABEL)/{unit,program}
	LD_LIBRARY_PATH=target LD_PRELOAD=libchalloc_dev.so target/run_unit_benchs $(LABEL)/unit
	LD_LIBRARY_PATH=target target/run_program_benchs $(LABEL)/program
	mkdir -p rapport/bench_results/$(LABEL)
	python3 benchmarks/plot_benchmarks.py benchmarks/results/$(LABEL) rapport/bench_results/$(LABEL)

clean:
	rm -rf target

rapport: rapport/*.typ rapport/images/* rapport/bench_results/** | target
	typst compile rapport/rapport.typ rapport/rapport.pdf