CC ?= gcc
CSTD ?= -std=c11
WARN := -Wall -Wextra -Werror
OPT ?= -O2 -g

INCLUDES := -Iinclude
CFLAGS := $(CSTD) $(WARN) $(OPT) $(INCLUDES)

# Absolute path to the shared test vectors, so the test binary finds them no
# matter which directory it is started from.
TESTVECTOR_DIR := $(abspath $(CURDIR)/../testvectors)
TEST_CFLAGS := -DBASE85N_TESTVECTOR_DIR=\"$(TESTVECTOR_DIR)\"

SRC := src/base85n.c
OBJ := $(SRC:.c=.o)
LIB := libbase85n.a

TEST_SRC := tests/test_base85n.c
TEST_BIN := tests/test_base85n

.PHONY: all test clean

all: $(LIB)

$(LIB): $(OBJ)
	ar rcs $@ $(OBJ)

src/%.o: src/%.c include/base85n.h
	$(CC) $(CFLAGS) -c $< -o $@

# `make test` builds and runs the test binary. It is built with
# -fsanitize=address,undefined whenever the toolchain supports that
# (detected on the fly by a throwaway compile), and falls back to a
# plain build otherwise so `make test` still works with a minimal
# gcc/clang + make toolchain.
#
# The whole recipe is one shell invocation, so it runs under `set -e`:
# without it a failing compile does not stop the recipe, and the final
# line happily runs whatever test binary an earlier build left behind --
# reporting PASS for source that no longer compiles. The stale binary is
# also removed up front so that failure mode cannot come back.
test:
	@set -e; \
	rm -f $(TEST_BIN); \
	SANFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"; \
	TMPC=$$(mktemp /tmp/base85n_sanXXXXXX.c); \
	TMPO=$$(mktemp /tmp/base85n_sanXXXXXX); \
	trap 'rm -f "$$TMPC" "$$TMPO"' EXIT; \
	echo 'int main(void){return 0;}' > $$TMPC; \
	if $(CC) $$SANFLAGS $$TMPC -o $$TMPO >/dev/null 2>&1; then \
		echo "Building tests with $$SANFLAGS"; \
		$(CC) $(CFLAGS) $(TEST_CFLAGS) $$SANFLAGS $(INCLUDES) $(TEST_SRC) $(SRC) -o $(TEST_BIN); \
	else \
		echo "Sanitizers not supported by $(CC); building plain test binary"; \
		$(CC) $(CFLAGS) $(TEST_CFLAGS) $(INCLUDES) $(TEST_SRC) $(SRC) -o $(TEST_BIN); \
	fi; \
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(LIB) $(TEST_BIN)
