MAKEFLAGS += -r
.PHONY: all sanitize clean
.SUFFIXES: .c .o .d
SRC := $(shell find . -path ./src -prune -o -type f -name "*.c" -print)
OBJ := $(patsubst %.c, %.o, $(SRC))
DEP := $(patsubst %.c, %.d, $(SRC))

SRCDIR = $(CURDIR)/../src
CFLAGS ?= -O3 -fno-plt -pipe
CFLAGS += -D_GNU_SOURCE=1 -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses -I$(CURDIR) -I$(SRCDIR)
LDFLAGS ?= -Wl,-O1
LDLIBS += -lm
SANFLAGS = -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

all: main
	$(CURDIR)/main

sanitize: main.sanitize
	$(CURDIR)/main.sanitize

main: $(OBJ)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

main.sanitize: $(SRC)
	$(LINK.c) $(SANFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c
	$(COMPILE.c) $< -o $@

clean:
	$(RM) -- $(OBJ) $(DEP) main main.sanitize

-include $(DEP)
%.d: %.c
	$(COMPILE.c) -MM $< -o $@

.SECONDARY: $(OBJ)
