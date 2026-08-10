CC ?= gcc
CSTD ?= -std=c11
WARN := -Wall -Wextra -Werror
OPT ?= -O2

BASE85N_DIR := ../../c
INCLUDES := -I$(BASE85N_DIR)/include
CFLAGS := $(CSTD) $(WARN) $(OPT) $(INCLUDES)

BIN := bench_speed
SRC := bench_speed.c $(BASE85N_DIR)/src/base85n.c

CORPUS := $(wildcard ../corpus/*)

SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all

.PHONY: all run check clean

all: $(BIN)

$(BIN): $(SRC) $(BASE85N_DIR)/include/base85n.h
	$(CC) $(CFLAGS) $(SRC) -o $@

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

clean:
	rm -f $(BIN) $(BIN)_asan
