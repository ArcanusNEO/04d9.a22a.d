MAKEFLAGS += -r
.PHONY: all clean
.SUFFIXES: .c .o .d
SRC := $(shell find . -path ./src -prune -o -type f -name "*.c" -print)
OBJ := $(patsubst %.c, %.o, $(SRC))
DEP := $(patsubst %.c, %.d, $(SRC))

CFLAGS ?= -O3 -fno-plt -pipe
CFLAGS += -D_GNU_SOURCE=1 -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses -I$(CURDIR)
LDFLAGS ?= -Wl,-O1
LDLIBS += -lm

all: main

main: $(OBJ)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c
	$(COMPILE.c) $< -o $@

clean:
	$(RM) -- $(OBJ) $(DEP) main

-include $(DEP)
%.d: %.c
	$(COMPILE.c) -MM $< -o $@

.SECONDARY: $(OBJ)
