CC ?= gcc
CSTD ?= -std=c11
WARN := -Wall -Wextra -Werror
OPT ?= -O2

BASE85N_DIR := ../../c
INCLUDES := -I$(BASE85N_DIR)/include
CFLAGS := $(CSTD) $(WARN) $(OPT) $(INCLUDES)

BIN := bench_speed
SRC := bench_speed.c $(BASE85N_DIR)/src/base85n.c

# The --binary attribution benchmark builds three extra encoder dialects
# into base85n.c. They exist only under this macro; see bench_binary_flag.c.
FLAGBIN := bench_binary_flag
FLAGSRC := bench_binary_flag.c $(BASE85N_DIR)/src/base85n.c
FLAGDEF := -DBASE85N_BENCH_ENCODERS

CORPUS := $(wildcard ../corpus/*)
# Which corpus files count as "representative binary data" for the --binary
# decision. Text and structured-text samples are measured too, by `run`.
BINCORPUS := $(wildcard ../corpus/*.wasm ../corpus/*.so ../corpus/*.ttf \
                        ../corpus/*.tar ../corpus/*.jpg ../corpus/*.png)

SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all

.PHONY: all run check binary-flag binary-flag-all binary-flag-check binary-flag-selftest clean

all: $(BIN) $(FLAGBIN)

$(BIN): $(SRC) $(BASE85N_DIR)/include/base85n.h
	$(CC) $(CFLAGS) $(SRC) -o $@

$(FLAGBIN): $(FLAGSRC) $(BASE85N_DIR)/include/base85n.h
	$(CC) $(CFLAGS) $(FLAGDEF) $(FLAGSRC) -o $@

# Runs against the two synthetic buffers plus every file the corpus
# fetcher has materialised. Run `python3 ../corpus.py` first to populate it.
run: $(BIN)
	./$(BIN) $(filter-out ../corpus/_archives,$(CORPUS))

# A heap bug in a benchmark harness quietly poisons the numbers rather
# than announcing itself, so the same code is also run under the
# sanitizers. Timings from this build are meaningless; only its exit
# status matters.
check: $(SRC) $(BASE85N_DIR)/include/base85n.h
	$(CC) $(CFLAGS) $(SANFLAGS) $(SRC) -o $(BIN)_asan
	./$(BIN)_asan $(filter-out ../corpus/_archives,$(CORPUS)) > /dev/null
	@echo "sanitizer run clean"

# The --binary decision, over the binary half of the corpus: the inputs
# such a flag would be pointed at.
binary-flag: $(FLAGBIN)
	./$(FLAGBIN) $(BINCORPUS)

# The same, over everything, including the text a --binary flag would be
# the wrong answer for.
binary-flag-all: $(FLAGBIN)
	./$(FLAGBIN) $(filter-out ../corpus/_archives,$(CORPUS))

# The claim that widening the skip's gate left the output alone, checked
# over generated shapes rather than only over the corpus. Fast enough to run
# without the sanitizers; `binary-flag-check` runs it under them too.
binary-flag-selftest: $(FLAGBIN)
	./$(FLAGBIN) --selftest 30000

binary-flag-check: $(FLAGSRC) $(BASE85N_DIR)/include/base85n.h
	$(CC) $(CFLAGS) $(FLAGDEF) $(SANFLAGS) $(FLAGSRC) -o $(FLAGBIN)_asan
	./$(FLAGBIN)_asan --selftest 3000
	./$(FLAGBIN)_asan $(filter-out ../corpus/_archives,$(CORPUS)) > /dev/null
	@echo "sanitizer run clean"

clean:
	rm -f $(BIN) $(BIN)_asan $(FLAGBIN) $(FLAGBIN)_asan
