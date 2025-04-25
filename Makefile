CC = gcc
# CFLAGS for development/debug builds (stricter checks)
CFLAGS = -std=c99 -Wall -Wextra -Werror -Wstrict-prototypes -g # Added -g for debug symbols
# RELEASE_CFLAGS for optimized release builds
RELEASE_CFLAGS = -std=c99 -Wall -Wextra -O2 -DNDEBUG # -O2 optimization, NDEBUG disables asserts

TARGET = llm_ctx
SRC = main.c gitignore.c
TEST_SRC = tests/test_gitignore.c tests/test_cli.c tests/test_stdin.c tests/test_config.c
TEST_TARGETS = tests/test_gitignore tests/test_cli tests/test_stdin tests/test_config
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

# Determine OS for symlink command
UNAME := $(shell uname)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

# New target for release build
release: $(SRC)
	@echo "Building release version ($(TARGET))..."
	$(CC) $(RELEASE_CFLAGS) -o $(TARGET) $^
	@echo "Stripping debug symbols from $(TARGET)..."
	strip $(TARGET)
	@echo "Release build complete: $(TARGET)"

# Note: test_gitignore depends on gitignore.c
tests/test_gitignore: tests/test_gitignore.c gitignore.c
	$(CC) $(CFLAGS) -o $@ $^

# Note: test_cli depends on main.c (for config parsing logic) and gitignore.c
# test_cli is an integration test, it runs the main llm_ctx executable.
# It needs its own source file compiled AND the source file containing the
# test_cli_config_discovery_binary_dir function.
tests/test_cli: tests/test_cli.c tests/test_config_binary_dir.c
	$(CC) $(CFLAGS) -o $@ $^ # Use $^ to link all prerequisites

# Note: test_config depends on main.c (for config parsing logic) and gitignore.c
# test_config is an integration test, it runs the main llm_ctx executable.
# It only needs its own source file compiled.
tests/test_config: tests/test_config.c
	$(CC) $(CFLAGS) -o $@ $< # Use $< to only link the first prerequisite (test_config.c)

tests/test_stdin: tests/test_stdin.c
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TARGETS)
	@echo "Backing up user config file..."
	@mv -f ".llm_ctx.conf" "__USER_llm_ctx.conf.backup__" 2>/dev/null || true
	@echo "Creating temporary empty config file..."
	@touch .llm_ctx.conf # Create dummy to stop upward search
	@echo "\nRunning unit tests..."
	@./tests/test_gitignore || true
	@echo "\nRunning integration tests..."
	@./tests/test_cli || true
	@echo "\nRunning config parser tests..."
	@./tests/test_config || true
	@echo "\nRunning stdin pipe tests..."
	@./tests/test_stdin || true
	@echo "\nRemoving temporary config file..."
	@rm -f ".llm_ctx.conf" # Remove dummy
	@echo "\nRestoring user config file..."
	@mv -f "__USER_llm_ctx.conf.backup__" ".llm_ctx.conf" 2>/dev/null || true
	@echo "Test run complete."
	@# Exit with non-zero status if any test failed (requires more complex tracking or a test runner)

clean:
	rm -f $(TARGET) $(TEST_TARGETS)

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

.PHONY: all clean install test symlink retest
