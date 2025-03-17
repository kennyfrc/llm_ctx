CC = gcc
CFLAGS = -std=c99 -Wall -Wextra
TARGET = llm_ctx
SRC = main.c gitignore.c
TEST_SRC = tests/test_gitignore.c tests/test_cli.c
TEST_TARGETS = tests/test_gitignore tests/test_cli
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

# Determine OS for symlink command
UNAME := $(shell uname)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

tests/test_gitignore: tests/test_gitignore.c gitignore.c
	$(CC) $(CFLAGS) -o $@ $^

tests/test_cli: tests/test_cli.c
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TARGETS)
	@echo "\nRunning unit tests..."
	./tests/test_gitignore
	@echo "\nRunning integration tests..."
	./tests/test_cli

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
