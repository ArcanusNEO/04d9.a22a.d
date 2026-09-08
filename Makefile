MAKEFLAGS += -r
.PHONY: all clean test sanitize

all:
	$(MAKE) -C src -- all

test: all
	$(CURDIR)/src/main --self-test
	$(MAKE) -C test -- all

sanitize: all
	$(MAKE) -C test -- sanitize

clean:
	$(MAKE) -C src -- clean
	$(MAKE) -C test -- clean
