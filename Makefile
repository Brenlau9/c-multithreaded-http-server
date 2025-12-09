# Executable name
EXECBIN  = httpserver

# Source / header discovery
SOURCES  = $(wildcard *.c)
HEADERS  = $(wildcard *.h)
OBJECTS  = $(SOURCES:%.c=%.o)

# Unit test binaries
UNIT_TESTS = tests/unit/test_queue tests/unit/test_rwlock

# Formatting
FORMATS  = $(SOURCES:%.c=.format/%.c.fmt) $(HEADERS:%.h=.format/%.h.fmt)

CC       = clang
FORMAT   = clang-format
CFLAGS   = -gdwarf-4 -Wall -Wpedantic -Werror -Wextra -DDEBUG

.PHONY: all clean nuke format

all: $(EXECBIN)

# Link final executable from all object files
$(EXECBIN): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

# Compile each .c into a .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	find . -name '*.o' -delete
	rm -f $(EXECBIN) $(OBJECTS) $(UNIT_TESTS)

nuke: clean
	rm -rf .format

# ------- Formatting targets (clang-format) -------

format: $(FORMATS)

.format/%.c.fmt: %.c
	mkdir -p .format
	$(FORMAT) -i $<
	touch $@

.format/%.h.fmt: %.h
	mkdir -p .format
	$(FORMAT) -i $<
	touch $@

.PHONY: check-format
check-format:
	@echo "Checking code formatting..."
	@FILES="$$(find . -name '*.c' -o -name '*.h')"; \
	if [ -n "$$FILES" ]; then \
		clang-format -style=file -Werror --dry-run $$FILES; \
	fi
	@echo "Formatting OK"

# ------- Unit test binaries -------

tests/unit/test_queue: tests/unit/test_queue.o queue.o
	$(CC) $(CFLAGS) -o $@ $^

tests/unit/test_rwlock: tests/unit/test_rwlock.o rwlock.o
	$(CC) $(CFLAGS) -o $@ $^

# ------- Tests -------

.PHONY: test

test: $(EXECBIN) $(UNIT_TESTS)
	./tests/unit/test_queue
	./tests/unit/test_rwlock
	./tests/integration/test_cli.sh
	./tests/integration/test_endpoints.sh
	./tests/integration/test_put_handler.sh
	./tests/integration/test_concurrency.sh