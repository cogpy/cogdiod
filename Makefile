# CogDiod Makefile

CC      = cc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -pthread \
          -Iinclude \
          -D_GNU_SOURCE \
          -DDISVM_NREGS=16 \
          -DDISVM_STKMAX=4096

LDFLAGS = -lpthread -lm

SRCS = src/kernel/cogdiod_kernel.c \
       src/kernel/pln.c \
       src/p9/distyx.c \
       src/elbo/elm_loader.c \
       packages/concept_node/concept_node_pkg.c \
       packages/evaluation_link/evaluation_link_pkg.c \
       packages/implication_link/implication_link_pkg.c

TEST_SRC = tests/test_cogdiod.c

OBJ = $(SRCS:.c=.o)
TEST_BIN = cogdiod_test

.PHONY: all test clean

all: $(TEST_BIN)

$(TEST_BIN): $(OBJ) $(TEST_SRC)
	$(CC) $(CFLAGS) $(OBJ) $(TEST_SRC) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(TEST_BIN)
