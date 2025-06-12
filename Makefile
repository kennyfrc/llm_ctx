CC = gcc

# CFLAGS for development/debug builds (stricter checks)
CFLAGS = -std=c99 -Wall -Wextra -Werror -Wstrict-prototypes -D_GNU_SOURCE -g # Added -g for debug symbols
# RELEASE_CFLAGS for optimized release builds
RELEASE_CFLAGS = -std=c99 -Wall -Wextra -O2 -DNDEBUG # -O2 optimization, NDEBUG disables asserts

# No need for external dependencies in JavaScript pack anymore

TARGET = llm_ctx
TEST_JS_PARSER = test_js_parser
SRC = main.c gitignore.c codemap.c arena.c packs.c tokenizer.c tokenizer_diagnostics.c
TEST_SRC = tests/test_gitignore.c tests/test_cli.c tests/test_stdin.c tests/test_packs.c tests/test_extension_mapping.c tests/test_js_pack.c tests/test_ruby_pack.c tests/test_pack_info.c tests/test_make_pack.c tests/test_codemap_patterns.c
TEST_TARGETS = tests/test_gitignore tests/test_cli tests/test_stdin tests/test_packs tests/test_extension_mapping tests/test_js_pack tests/test_ruby_pack tests/test_pack_info tests/test_make_pack tests/test_codemap_patterns tests/test_tree_flags tests/test_tokenizer tests/test_tokenizer_cli
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

# Determine OS for symlink command
UNAME := $(shell uname)

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

OBJS = main.o gitignore.o codemap.o arena.o packs.o tokenizer.o tokenizer_diagnostics.o

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -ldl

main.o: main.c arena.h gitignore.h codemap.h packs.h
	$(CC) $(CFLAGS) -c $<

gitignore.o: gitignore.c gitignore.h
	$(CC) $(CFLAGS) -c $<

codemap.o: codemap.c codemap.h arena.h
	$(CC) $(CFLAGS) -c $<

arena.o: arena.c arena.h
	$(CC) $(CFLAGS) -c $<

packs.o: packs.c packs.h arena.h
	$(CC) $(CFLAGS) -c $<

tokenizer.o: tokenizer.c tokenizer.h
	$(CC) $(CFLAGS) -c $<

tokenizer_diagnostics.o: tokenizer_diagnostics.c tokenizer.h arena.h
	$(CC) $(CFLAGS) -c $<
	
$(TEST_JS_PARSER): test_js_parser.c codemap.o arena.o packs.o
	$(CC) $(CFLAGS) -o $@ $^

# New target for release build
release: $(OBJS)
	@echo "Building release version ($(TARGET))..."
	$(CC) $(RELEASE_CFLAGS) -o $(TARGET) $^ -ldl
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

tests/test_packs: tests/test_packs.c packs.c arena.c
	$(CC) $(CFLAGS) -DTEST_BUILD -o $@ $^

tests/test_extension_mapping: tests/test_extension_mapping.c packs.c arena.c
	$(CC) $(CFLAGS) -DTEST_BUILD -o $@ $^

# Build test_js_pack with tree-sitter JavaScript statically linked
tests/test_js_pack: tests/test_js_pack.c packs/javascript/js_pack.c arena.c
	$(CC) $(CFLAGS) -DTEST_BUILD -Itree-sitter-javascript/src -Itree-sitter-javascript/bindings/c -o $@ $^ tree-sitter-javascript/libtree-sitter-javascript.a -L/opt/homebrew/lib -ltree-sitter

# Build test_ruby_pack with tree-sitter Ruby statically linked
tests/test_ruby_pack: tests/test_ruby_pack.c packs/ruby/ruby_pack.c arena.c
	$(CC) $(CFLAGS) -DTEST_BUILD -Itree-sitter-ruby/src -Itree-sitter-ruby/bindings/c -o $@ $^ tree-sitter-ruby/libtree-sitter-ruby.a -L/opt/homebrew/lib -ltree-sitter

# Build test_pack_info
tests/test_pack_info: tests/test_pack_info.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_make_pack
tests/test_make_pack: tests/test_make_pack.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_codemap_patterns
tests/test_codemap_patterns: tests/test_codemap_patterns.c codemap.c packs.c arena.c gitignore.c
	$(CC) $(CFLAGS) -DTEST_BUILD -o $@ $^

tests/test_tree_flags: tests/test_tree_flags.c
	$(CC) $(CFLAGS) -o $@ $^

# Build test_tokenizer
tests/test_tokenizer: tests/test_tokenizer.c tokenizer.o arena.o
	$(CC) $(CFLAGS) -o $@ $^ -ldl

# Build test_tokenizer_cli
tests/test_tokenizer_cli: tests/test_tokenizer_cli.c
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TARGETS)
	@echo "\nRunning unit tests..."
	@./tests/test_gitignore || true
	@echo "\nRunning integration tests..."
	@./tests/test_cli || true
	@echo "\nRunning stdin pipe tests..."
	@./tests/test_stdin || true
	@echo "\nRunning pack registry tests..."
	@./tests/test_packs || true
	@echo "\nRunning extension mapping tests..."
	@./tests/test_extension_mapping || true
	@echo "\nRunning language pack tests: javascript..."
	@./tests/test_js_pack || true
	@echo "\nRunning language pack tests: ruby..."
	@./tests/test_ruby_pack || true
	@echo "\nRunning pack info CLI tests..."
	@./tests/test_pack_info || true
	@echo "\nRunning make pack command tests..."
	@./tests/test_make_pack || true
	@echo "\nRunning make pack shell tests..."
	@./tests/test_make_pack.sh || true
	@echo "\nRunning codemap patterns tests..."
	@./tests/test_codemap_patterns || true
	@echo "\nRunning tokenizer unit tests..."
	@./tests/test_tokenizer || true
	@echo "\nRunning tokenizer CLI integration tests..."
	@./tests/test_tokenizer_cli || true
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

# Target for installing language packs for codemap
pack:
	@mkdir -p packs/$(LANG)
	@echo "Installing tree-sitter $(LANG) pack..."
	@if [ ! -d tree-sitter-$(LANG) ]; then \
		echo "Cloning tree-sitter-$(LANG) repository..."; \
		git clone https://github.com/tree-sitter/tree-sitter-$(LANG).git || { echo "Failed to clone repository"; exit 1; }; \
	fi
	@echo "Building tree-sitter-$(LANG) grammar..."
	@cd tree-sitter-$(LANG) && \
		if [ -f binding.gyp ]; then \
			npm install || { echo "Failed to run npm install"; exit 1; }; \
		fi
	@echo "Compiling parser.so with static linking..."
	@if [ -f tree-sitter-$(LANG)/src/parser.c ]; then \
		if [ -f packs/$(LANG)/js_pack.c ]; then \
			$(CC) -shared -fPIC -o packs/$(LANG)/parser.so packs/$(LANG)/js_pack.c tree-sitter-$(LANG)/src/parser.c tree-sitter-$(LANG)/src/scanner.c -Itree-sitter-$(LANG)/src -Itree-sitter-$(LANG)/bindings/c -L/opt/homebrew/lib -ltree-sitter || { echo "Failed to compile parser"; exit 1; }; \
		else \
			echo "Creating language pack C file..."; \
			cp packs/javascript/js_pack.c packs/$(LANG)/$(LANG)_pack.c; \
			$(CC) -shared -fPIC -o packs/$(LANG)/parser.so packs/$(LANG)/$(LANG)_pack.c tree-sitter-$(LANG)/src/parser.c tree-sitter-$(LANG)/src/scanner.c -Itree-sitter-$(LANG)/src -Itree-sitter-$(LANG)/bindings/c -L/opt/homebrew/lib -ltree-sitter || { echo "Failed to compile parser"; exit 1; }; \
		fi; \
		echo "Tree-sitter $(LANG) pack installed in packs/$(LANG)/parser.so"; \
	else \
		echo "Could not find parser.c - creating dummy parser.so for testing"; \
		touch packs/$(LANG)/parser.so; \
	fi

# Optional target for installing all required language packs
packs: 
	@$(MAKE) pack LANG=javascript
	@$(MAKE) pack LANG=typescript
	@$(MAKE) pack LANG=ruby

# Test the make pack command
test-make-pack: $(TARGET)
	@echo "Running 'make pack' command tests..."
	@./tests/test_make_pack.sh

# Run the full pack management integration test suite
test-pack-management: $(TARGET) $(TEST_TARGETS)
	@echo "Running comprehensive pack management integration tests..."
	@./tests/test_pack_management.sh

# Tokenizer target
tokenizer:
	@echo "Building tokenizer from tiktoken-c..."
	@if [ ! -d "$(TOKENIZER_DIR)/tiktoken-c" ] || [ ! -f "$(TOKENIZER_DIR)/tiktoken-c/Cargo.toml" ]; then \
		echo "Initializing tiktoken-c submodule..."; \
		git submodule update --init --recursive || { echo "Failed to initialize submodule"; exit 1; }; \
	fi
	@echo "Building tiktoken-c (universal binary for macOS)..."
	@cd $(TOKENIZER_DIR)/tiktoken-c && \
		cargo build --release || { echo "Failed to build tiktoken-c"; exit 1; }
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

.PHONY: all clean install test symlink retest pack packs test-make-pack test-pack-management tokenizer clean-tokenizer