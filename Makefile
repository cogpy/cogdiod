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
       src/kernel/cogdiod_log.c \
       src/p9/distyx.c \
       src/elbo/elm_loader.c \
       src/elbo/elbo_compiler.c \
       packages/concept_node/concept_node_pkg.c \
       packages/evaluation_link/evaluation_link_pkg.c \
       packages/implication_link/implication_link_pkg.c

TEST_SRC = tests/test_cogdiod.c

OBJ = $(SRCS:.c=.o)
TEST_BIN = cogdiod_test

BRIDGE_SRC = cogdiod-lang/bridge/cogdiod_bridge.c
BRIDGE_BIN = cogdiod_bridge

CLI_SRC = tools/cogdiod_cli.c
CLI_BIN = cogdiod_cli

TEST_PLN_BIN   = test_pln
TEST_ELM_BIN   = test_elm_loader
TEST_DISTYX_BIN= test_distyx
TEST_ECAN_BIN  = test_ecan

.PHONY: all test clean bridge tools test-extra

all: $(TEST_BIN)

bridge: $(BRIDGE_BIN)

$(BRIDGE_BIN): $(BRIDGE_SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

tools: $(CLI_BIN)

$(CLI_BIN): $(CLI_SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(TEST_PLN_BIN): $(OBJ) tests/test_pln.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_pln.c -o $@ $(LDFLAGS)

$(TEST_ELM_BIN): $(OBJ) tests/test_elm_loader.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_elm_loader.c -o $@ $(LDFLAGS)

$(TEST_DISTYX_BIN): $(OBJ) tests/test_distyx.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_distyx.c -o $@ $(LDFLAGS)

$(TEST_ECAN_BIN): $(OBJ) tests/test_ecan.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_ecan.c -o $@ $(LDFLAGS)

test-extra: $(TEST_PLN_BIN) $(TEST_ELM_BIN) $(TEST_DISTYX_BIN) $(TEST_ECAN_BIN)
	./$(TEST_PLN_BIN)
	./$(TEST_ELM_BIN)
	./$(TEST_DISTYX_BIN)
	./$(TEST_ECAN_BIN)


$(TEST_BIN): $(OBJ) $(TEST_SRC)
	$(CC) $(CFLAGS) $(OBJ) $(TEST_SRC) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(TEST_BIN) $(BRIDGE_BIN) $(CLI_BIN) \
	      $(TEST_PLN_BIN) $(TEST_ELM_BIN) $(TEST_DISTYX_BIN) $(TEST_ECAN_BIN)
