CC ?= cc
GCC ?= gcc
CPPFLAGS ?=
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

WARNFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
SANFLAGS = -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

.PHONY: all test sanitize analyze clean

all: build/main

build:
	mkdir -p $@

build/main: main.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

build/main-test: main-test.c main.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

test: build/main build/main-test
	./build/main --self-test
	./build/main-test

build/main-test-sanitize: main-test.c main.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(LDFLAGS) $(SANFLAGS) -o $@ $< $(LDLIBS)

sanitize: build/main-test-sanitize
	./build/main-test-sanitize

analyze:
	$(GCC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -fanalyzer -c main.c -o /dev/null
	$(GCC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -fanalyzer -c main-test.c -o /dev/null

clean:
	$(RM) -r build
