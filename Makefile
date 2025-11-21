# Executable name
EXECBIN  = httpserver

# Source / header discovery
SOURCES  = $(wildcard *.c)
HEADERS  = $(wildcard *.h)
OBJECTS  = $(SOURCES:%.c=%.o)

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
	rm -f $(EXECBIN) $(OBJECTS)

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

.PHONY: test

test: $(EXECBIN)
	./tests/integration/test_cli.sh
	./tests/integration/test_endpoints.sh
	./tests/integration/test_concurrency.sh
