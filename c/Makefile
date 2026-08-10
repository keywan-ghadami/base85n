CC ?= gcc
CSTD ?= -std=c11
WARN := -Wall -Wextra -Werror
OPT ?= -O2 -g

INCLUDES := -Iinclude
CFLAGS := $(CSTD) $(WARN) $(OPT) $(INCLUDES)

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
test:
	@SANFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"; \
	TMPC=$$(mktemp /tmp/base85n_sanXXXXXX.c); \
	TMPO=$$(mktemp /tmp/base85n_sanXXXXXX); \
	echo 'int main(void){return 0;}' > $$TMPC; \
	if $(CC) $$SANFLAGS $$TMPC -o $$TMPO >/dev/null 2>&1; then \
		echo "Building tests with $$SANFLAGS"; \
		$(CC) $(CFLAGS) $$SANFLAGS $(INCLUDES) $(TEST_SRC) $(SRC) -o $(TEST_BIN); \
	else \
		echo "Sanitizers not supported by $(CC); building plain test binary"; \
		$(CC) $(CFLAGS) $(INCLUDES) $(TEST_SRC) $(SRC) -o $(TEST_BIN); \
	fi; \
	rm -f $$TMPC $$TMPO; \
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(LIB) $(TEST_BIN)
