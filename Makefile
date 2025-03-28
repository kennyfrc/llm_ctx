CC = gcc
# CFLAGS for development/debug builds (stricter checks)
CFLAGS = -std=c99 -Wall -Wextra -Werror -Wstrict-prototypes -g # Added -g for debug symbols
# RELEASE_CFLAGS for optimized release builds
RELEASE_CFLAGS = -std=c99 -Wall -Wextra -O2 -DNDEBUG # -O2 optimization, NDEBUG disables asserts

TARGET = llm_ctx
SRC = main.c gitignore.c
TEST_SRC = tests/test_gitignore.c tests/test_cli.c tests/test_stdin.c
TEST_TARGETS = tests/test_gitignore tests/test_cli tests/test_stdin
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

tests/test_gitignore: tests/test_gitignore.c gitignore.c
	$(CC) $(CFLAGS) -o $@ $^

tests/test_cli: tests/test_cli.c
	$(CC) $(CFLAGS) -o $@ $^

tests/test_stdin: tests/test_stdin.c
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TARGETS)
	@echo "\nRunning unit tests..."
	./tests/test_gitignore
	@echo "\nRunning integration tests..."
	./tests/test_cli
	@echo "\nRunning stdin pipe tests..."
	./tests/test_stdin

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

.PHONY: all clean install test symlink
