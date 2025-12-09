# =========================
# Build configuration
# =========================

# Executable name
EXECBIN  = httpserver

# Source / header discovery (root only)
SOURCES  = $(wildcard *.c)
HEADERS  = $(wildcard *.h)
OBJECTS  = $(SOURCES:%.c=%.o)

# Unit test binaries
UNIT_TESTS = tests/unit/test_queue tests/unit/test_rwlock

# Tools and flags
CC      ?= clang
CFLAGS  ?= -gdwarf-4 -Wall -Wextra -Wpedantic -Werror -DDEBUG
LDFLAGS ?= -pthread
FORMAT  ?= clang-format

# Default target
.PHONY: all
all: $(EXECBIN)

# =========================
# Main binary
# =========================

$(EXECBIN): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# =========================
# Unit test binaries
# =========================

tests/unit/test_queue: tests/unit/test_queue.o queue.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/unit/test_rwlock: tests/unit/test_rwlock.o rwlock.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Let make use the same pattern rule for .o files in tests/unit/
tests/unit/%.o: tests/unit/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# =========================
# Formatting
# =========================

.PHONY: format
format:
	@echo "Running clang-format on all .c/.h files..."
	@FILES="$$(find . -name '*.c' -o -name '*.h')"; \
	if [ -n "$$FILES" ]; then \
		$(FORMAT) -i $$FILES; \
	fi
	@echo "Formatting complete."

.PHONY: check-format
check-format:
	@echo "Checking code formatting..."
	@FILES="$$(find . -name '*.c' -o -name '*.h')"; \
	if [ -n "$$FILES" ]; then \
		$(FORMAT) -style=file -Werror --dry-run $$FILES; \
	fi
	@echo "Formatting OK"

# =========================
# Tests
# =========================

.PHONY: test
test: $(EXECBIN) $(UNIT_TESTS)
	./tests/unit/test_queue
	./tests/unit/test_rwlock
	./tests/integration/test_cli.sh
	./tests/integration/test_endpoints.sh
	./tests/integration/test_put_handler.sh
	./tests/integration/test_concurrency.sh

# =========================
# Cleaning
# =========================

.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS)
	rm -f $(EXECBIN)
	rm -f $(UNIT_TESTS)
	rm -f tests/unit/*.o
	# If .format existed before, this keeps things tidy
	rm -rf .format
	@echo "Clean complete."
