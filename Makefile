# Genesis+ build file.
#
#   make              Build the compiler (./genesis or genesis.exe)
#   make test         Build, then run every automated test (stops on failure)
#   make test-unit    Unit, golden-file and fuzz tests
#   make test-cli     Command-line and dev-server tests (needs a POSIX shell)
#   make test-extension  VS Code extension tests (needs Node.js 18+)
#   make check-eol    Fail if any text file has CRLF line endings
#   make examples     Compile every example project
#   make sanitize     All C tests under AddressSanitizer + UBSan (Linux/macOS only)
#   make install      Copy the compiler to $(PREFIX)/bin (default /usr/local)
#   make analyze      Run GCC's static analyzer over the sources
#   make clean        Remove build output
#
# On Windows, run these from an MSYS2 UCRT64 shell (see README.md).

ifeq ($(origin CC),default)
  CC = gcc
endif
CSTD = -std=c17
WARNINGS = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Wformat=2 -Wconversion -Wno-sign-conversion
OPT ?= -O2
CFLAGS ?= $(OPT) -g
ALL_CFLAGS = $(CSTD) $(WARNINGS) $(CFLAGS) -Iinclude
LDFLAGS ?=

ifeq ($(OS),Windows_NT)
  EXE = .exe
  LDLIBS = -lws2_32
else
  EXE =
  LDLIBS =
endif

TARGET = genesis$(EXE)
BUILD = build

CORE_SOURCES = src/buffer.c src/diag.c src/lexer.c src/ast.c src/parser.c \
               src/codegen.c src/platform.c src/compiler.c src/formatter.c \
               src/server.c
CORE_OBJECTS = $(CORE_SOURCES:src/%.c=$(BUILD)/%.o)

TEST_SOURCES = tests/unit/test_main.c tests/unit/test_lexer.c \
               tests/unit/test_parser.c tests/unit/test_codegen.c \
               tests/unit/test_formatter.c tests/unit/test_server.c \
               tests/unit/test_fuzz.c \
               tests/unit/test_golden.c
TEST_TARGET = $(BUILD)/unit_tests$(EXE)

EXAMPLES = $(wildcard examples/*/lead.gns)

PREFIX ?= /usr/local

.PHONY: all test test-unit test-cli test-extension check-eol examples sanitize         analyze clean install

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(TARGET): $(CORE_OBJECTS) $(BUILD)/main.o
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(TEST_TARGET): $(CORE_OBJECTS) $(TEST_SOURCES) tests/unit/test.h
	$(CC) $(ALL_CFLAGS) -Itests/unit $(LDFLAGS) $(CORE_OBJECTS) $(TEST_SOURCES) \
		-o $@ $(LDLIBS)

# Absolute path of the freshly built native compiler, passed to the
# extension tests so they never pick up another binary.
GENESIS_BIN_PATH = $(abspath $(TARGET))
NODE := $(shell command -v node 2>/dev/null)

test: check-eol test-unit test-cli test-extension

check-eol:
	sh tests/check_line_endings.sh

test-unit: $(TEST_TARGET)
	./$(TEST_TARGET)

test-cli: $(TARGET)
	sh tests/cli_test.sh ./$(TARGET)

test-extension: $(TARGET)
ifeq ($(NODE),)
	@echo "test-extension: SKIPPED (Node.js not found; install Node.js 18+ to run it)"
else
	cd vscode-extension && GENESIS_BIN="$(GENESIS_BIN_PATH)" node --test "test/*.test.js"
endif

examples: $(TARGET)
	@for f in $(EXAMPLES); do \
		echo "== $$f"; ./$(TARGET) build $$f || exit 1; \
	done

# Sanitizer builds need a toolchain that ships libasan/libubsan (GCC or Clang
# on Linux/macOS). MSYS2 UCRT64 GCC does not, so this target fails there.
sanitize:
ifeq ($(OS),Windows_NT)
	@echo "sanitize: not available on Windows (MSYS2 GCC has no libasan/libubsan)."
	@echo "Run it on Linux or macOS, or rely on the Linux CI job."
	@exit 1
else
	$(MAKE) clean
	$(MAKE) check-eol test-unit test-cli OPT=-O1 CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" \
		LDFLAGS="-fsanitize=address,undefined"
endif

analyze: | $(BUILD)
	@for f in $(CORE_SOURCES) src/main.c; do \
		echo "analyzing $$f"; \
		$(CC) $(ALL_CFLAGS) -fanalyzer -c $$f -o $(BUILD)/analyze.o || exit 1; \
	done
	rm -f $(BUILD)/analyze.o

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -rf $(BUILD) $(TARGET)
