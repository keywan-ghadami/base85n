# Fuzzing harnesses for the C implementation, and for it against the Rust one.
#
# libFuzzer, so clang. Every target is built with AddressSanitizer and
# UndefinedBehaviorSanitizer, because a fuzzer without them only finds the
# bugs that happen to crash, and the ones worth finding here do not.

# make defines CC as a built-in, so `?=` would not reach it: this needs a name
# of its own. libFuzzer is a clang feature; gcc cannot build these.
FUZZ_CC ?= clang
BASE85N_DIR := ..
INCLUDES := -I$(BASE85N_DIR)/include
SAN := -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
          $(INCLUDES) $(SAN)

SRC := $(BASE85N_DIR)/src/base85n.c
BINS := fuzz_roundtrip fuzz_decode

# How long each target runs under `make run`. CI passes a small number; a
# real campaign passes a large one or none at all.
SECS ?= 60
RUNFLAGS := -max_total_time=$(SECS) -print_final_stats=1 -rss_limit_mb=4096

RUST_DIR := $(BASE85N_DIR)/../rust
RUST_LIB := $(RUST_DIR)/target/release/libbase85n.a
RUST_RENAMED := rust_renamed.a

.PHONY: all run run-differential corpus clean differential

all: $(BINS)

fuzz_%: fuzz_%.c $(SRC) $(BASE85N_DIR)/include/base85n.h
	$(FUZZ_CC) $(CFLAGS) $< $(SRC) -o $@

# Both implementations export the same symbol names -- that is the point of
# the Rust crate's C ABI -- so they cannot be linked together as they are.
# Renaming one set is what makes an in-process comparison possible at all.
$(RUST_LIB):
	cd $(RUST_DIR) && cargo build --release --lib

$(RUST_RENAMED): $(RUST_LIB)
	objcopy --redefine-sym base85n_encode=rs_base85n_encode \
	        --redefine-sym base85n_decode=rs_base85n_decode \
	        --redefine-sym base85n_strerror=rs_base85n_strerror \
	        $(RUST_LIB) $@

differential: fuzz_differential
fuzz_differential: fuzz_differential.c $(SRC) $(RUST_RENAMED)
	$(FUZZ_CC) $(CFLAGS) fuzz_differential.c $(SRC) $(RUST_RENAMED) -o $@

# Seeds: the shared test vectors are the inputs already known to reach every
# construct, so the fuzzer starts from them rather than from nothing.
corpus:
	mkdir -p corpus
	python3 seed_corpus.py corpus

run: $(BINS) corpus
	./fuzz_roundtrip $(RUNFLAGS) corpus
	./fuzz_decode $(RUNFLAGS) corpus

run-differential: fuzz_differential corpus
	./fuzz_differential $(RUNFLAGS) corpus

clean:
	rm -rf $(BINS) fuzz_differential $(RUST_RENAMED) corpus crash-* leak-* timeout-*
