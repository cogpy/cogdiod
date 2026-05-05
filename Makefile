# Build configuration (restored after Copilot session loss in run 67505091145)
CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11 -pthread -Iinclude -D_GNU_SOURCE -DDISVM_NREGS=16 -DDISVM_STKMAX=4096
LDFLAGS ?= -lpthread -lm

SRCS = src/kernel/cogdiod_kernel.c \
       src/kernel/cogdiod_log.c \
       src/kernel/pln.c \
       src/p9/distyx.c \
       src/elbo/elm_loader.c \
       src/elbo/elbo_compiler.c \
       packages/concept_node/concept_node_pkg.c \
       packages/evaluation_link/evaluation_link_pkg.c \
       packages/implication_link/implication_link_pkg.c

TEST_SRC     = tests/test_cogdiod.c
UNIT_TESTS   = tests/test_pln.c tests/test_elm_loader.c \
               tests/test_distyx.c tests/test_ecan.c
UNIT_BINS    = test_pln test_elm_loader test_distyx test_ecan

OBJ = $(SRCS:.c=.o)
TEST_BIN = cogdiod_test

# CLI tool
CLI_BIN  = cogdiod-cli
CLI_SRC  = tools/cogdiod_cli.c

# Bridge server
BRIDGE_BIN = cogdiod_bridge
BRIDGE_SRC = cogdiod-lang/bridge/cogdiod_bridge.c

.PHONY: all test unit_tests clean tools bridge

all: $(TEST_BIN)

$(TEST_BIN): $(OBJ) $(TEST_SRC)
	$(CC) $(CFLAGS) $(OBJ) $(TEST_SRC) -o $@ $(LDFLAGS)

# Unit test binaries (each links with all kernel objects)
test_pln: $(OBJ) tests/test_pln.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_pln.c -o $@ $(LDFLAGS)

test_elm_loader: $(OBJ) tests/test_elm_loader.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_elm_loader.c -o $@ $(LDFLAGS)

test_distyx: $(OBJ) tests/test_distyx.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_distyx.c -o $@ $(LDFLAGS)

test_ecan: $(OBJ) tests/test_ecan.c
	$(CC) $(CFLAGS) $(OBJ) tests/test_ecan.c -o $@ $(LDFLAGS)

unit_tests: $(UNIT_BINS)
	./test_pln && ./test_elm_loader && ./test_distyx && ./test_ecan

# CLI tool
tools: $(CLI_BIN)
$(CLI_BIN): $(CLI_SRC)
	$(CC) $(CFLAGS) $(CLI_SRC) -o $@ $(LDFLAGS)

# Bridge server
bridge: $(BRIDGE_BIN)
$(BRIDGE_BIN): $(BRIDGE_SRC)
	$(CC) $(CFLAGS) $(BRIDGE_SRC) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(TEST_BIN) $(UNIT_BINS) $(CLI_BIN) $(BRIDGE_BIN)
