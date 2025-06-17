CC = gcc

# CFLAGS for development/debug builds (stricter checks)
CFLAGS = -std=c99 -Wall -Wextra -Werror -Wstrict-prototypes -D_GNU_SOURCE -g # Added -g for debug symbols
# RELEASE_CFLAGS for optimized release builds
RELEASE_CFLAGS = -std=c99 -Wall -Wextra -O2 -DNDEBUG # -O2 optimization, NDEBUG disables asserts

# No need for external dependencies in JavaScript pack anymore

TARGET = llm_ctx
TEST_JS_PARSER = test_js_parser
SRC = main.c gitignore.c arena.c tokenizer.c tokenizer_diagnostics.c config.c toml.c debug.c
TEST_SRC = tests/test_gitignore.c tests/test_cli.c tests/test_stdin.c
TEST_TARGETS = tests/test_gitignore tests/test_cli tests/test_stdin tests/test_tree_flags tests/test_tokenizer tests/test_tokenizer_cli tests/test_arena tests/test_config tests/test_filerank tests/test_keywords tests/test_filerank_cutoff
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

# Determine OS for symlink command
UNAME := $(shell uname)
ARCH := $(shell uname -m)

# Tokenizer configuration
TOKENIZER_DIR = tokenizer
TOKENIZER_LIB_NAME = libtiktoken_c
ifeq ($(UNAME), Darwin)
    DYNLIB_EXT = dylib
else ifeq ($(UNAME), Linux)
    DYNLIB_EXT = so
else
    DYNLIB_EXT = dll
endif
TOKENIZER_LIB = $(TOKENIZER_DIR)/$(TOKENIZER_LIB_NAME).$(DYNLIB_EXT)

all: $(TARGET) tokenizer

OBJS = main.o gitignore.o arena.o tokenizer.o tokenizer_diagnostics.o config.o toml.o debug.o

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -ldl -lm

main.o: main.c arena.h gitignore.h
	$(CC) $(CFLAGS) -c $<

gitignore.o: gitignore.c gitignore.h
	$(CC) $(CFLAGS) -c $<

arena.o: arena.c arena.h
	$(CC) $(CFLAGS) -c $<

tokenizer.o: tokenizer.c tokenizer.h
	$(CC) $(CFLAGS) -c $<

tokenizer_diagnostics.o: tokenizer_diagnostics.c tokenizer.h arena.h
	$(CC) $(CFLAGS) -c $<

config.o: config.c config.h arena.h
	$(CC) $(CFLAGS) -c $<

toml.o: toml.c toml.h
	$(CC) $(CFLAGS) -c $<

debug.o: debug.c debug.h
	$(CC) $(CFLAGS) -c $<
	
# New target for release build
release: $(OBJS)
	@echo "Building release version ($(TARGET))..."
	$(CC) $(RELEASE_CFLAGS) -o $(TARGET) $^ -ldl -lm
	@echo "Stripping debug symbols from $(TARGET)..."
	strip $(TARGET)
	@echo "Release build complete: $(TARGET)"

# Note: test_gitignore depends on gitignore.c
tests/test_gitignore: tests/test_gitignore.c gitignore.c
	$(CC) $(CFLAGS) -o $@ $^

# Note: test_cli depends on main.c (for config parsing logic) and gitignore.c
# test_cli is an integration test, it runs the main llm_ctx executable.
# test_cli is an integration test
tests/test_cli: tests/test_cli.c
	$(CC) $(CFLAGS) -o $@ $<

tests/test_stdin: tests/test_stdin.c
	$(CC) $(CFLAGS) -o $@ $^


tests/test_tree_flags: tests/test_tree_flags.c
	$(CC) $(CFLAGS) -o $@ $^

tests/test_arena: tests/test_arena.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_tokenizer
tests/test_tokenizer: tests/test_tokenizer.c tokenizer.o tokenizer_diagnostics.o arena.o
	$(CC) $(CFLAGS) -o $@ $^ -ldl

# Build test_tokenizer_cli
tests/test_tokenizer_cli: tests/test_tokenizer_cli.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_config
tests/test_config: tests/test_config.c config.o arena.o toml.o debug.o
	$(CC) $(CFLAGS) -o $@ $^

# Build test_filerank
tests/test_filerank: tests/test_filerank.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_keywords
tests/test_keywords: tests/test_keywords.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_filerank_cutoff
tests/test_filerank_cutoff: tests/test_filerank_cutoff.c
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TARGETS)
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_gitignore || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_arena || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_cli || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_stdin || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_tree_flags || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_tokenizer || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_tokenizer_cli || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_config || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_filerank || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_keywords || true
	@echo ""
	@LLM_CTX_NO_CONFIG=1 ./tests/test_filerank_cutoff || true
	@echo "Test run complete."
	@# Exit with non-zero status if any test failed (requires more complex tracking or a test runner)

clean:
	rm -f $(TARGET) $(TEST_TARGETS) *.o
	rm -f tokenizer/*.o tokenizer/tokenizer.o

install: $(TARGET)
	install -m 755 $(TARGET) $(BINDIR)

symlink: $(TARGET)
	@echo "Creating symlink in $(BINDIR)..."
ifeq ($(UNAME), Darwin)
	ln -sf "$(CURDIR)/$(TARGET)" "$(BINDIR)/$(TARGET)"
else ifeq ($(UNAME), Linux)
	ln -sf "$(CURDIR)/$(TARGET)" "$(BINDIR)/$(TARGET)"
else
	@echo "Unsupported platform for automatic symlink creation."
	@echo "To create a symlink manually, use:"
	@echo "  ln -sf \"$(CURDIR)/$(TARGET)\" \"<your-bin-directory>/$(TARGET)\""
endif
	@echo "Symlink created. You can now run '$(TARGET)' from anywhere."

# Shortcut to clean, build, and test
retest: clean test


# Tokenizer target
tokenizer:
	@echo "Building tokenizer from tiktoken-c..."
	@if [ ! -d "$(TOKENIZER_DIR)/tiktoken-c" ] || [ ! -f "$(TOKENIZER_DIR)/tiktoken-c/Cargo.toml" ]; then \
		echo "Initializing tiktoken-c submodule..."; \
		git submodule update --init --recursive || { echo "Failed to initialize submodule"; exit 1; }; \
	fi
	@echo "Building tiktoken-c for $(ARCH) architecture..."
	@cd $(TOKENIZER_DIR)/tiktoken-c && \
		PATH="$$HOME/.cargo/bin:$$PATH" cargo build --release || { echo "Failed to build tiktoken-c"; exit 1; }
	@echo "Copying library files..."
	@cp $(TOKENIZER_DIR)/tiktoken-c/target/release/$(TOKENIZER_LIB_NAME).$(DYNLIB_EXT) $(TOKENIZER_LIB) || { echo "Failed to copy library"; exit 1; }
	@if [ -f "$(TOKENIZER_DIR)/tiktoken-c/tiktoken.h" ]; then \
		cp $(TOKENIZER_DIR)/tiktoken-c/tiktoken.h $(TOKENIZER_DIR)/; \
	else \
		echo "Warning: Could not find tiktoken.h header"; \
	fi
	@echo "[PACK] tokenizer  ✔  $(TOKENIZER_LIB_NAME).$(DYNLIB_EXT) installed"

# Clean tokenizer build artifacts
clean-tokenizer:
	rm -rf $(TOKENIZER_DIR)/build
	rm -f $(TOKENIZER_LIB)
	rm -f $(TOKENIZER_DIR)/tiktoken.h

.PHONY: all clean install test symlink retest tokenizer clean-tokenizer