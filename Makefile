CC ?= cc
GCC ?= gcc
CPPFLAGS ?=
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

WARNFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
SANFLAGS = -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

.PHONY: all test sanitize analyze clean

all: build/a22a-dpi

build:
	mkdir -p $@

build/a22a-dpi: a22a-dpi.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

build/a22a-dpi-test: a22a-dpi-test.c a22a-dpi.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

test: build/a22a-dpi build/a22a-dpi-test
	./build/a22a-dpi --self-test
	./build/a22a-dpi-test

build/a22a-dpi-test-sanitize: a22a-dpi-test.c a22a-dpi.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(LDFLAGS) $(SANFLAGS) -o $@ $< $(LDLIBS)

sanitize: build/a22a-dpi-test-sanitize
	./build/a22a-dpi-test-sanitize

analyze:
	$(GCC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -fanalyzer -c a22a-dpi.c -o /dev/null
	$(GCC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -fanalyzer -c a22a-dpi-test.c -o /dev/null

clean:
	$(RM) -r build
