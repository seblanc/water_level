# Host unit tests for the pure-C parts of the a02yyuw component.
# Usage (from the project root):  make -C test/host run
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O0 -g
COMP    := ../../components/a02yyuw

SRCS := test_parser.c $(COMP)/a02yyuw_parser.c $(COMP)/a02yyuw_filter.c

.PHONY: all run clean

all: test_parser

test_parser: $(SRCS) $(COMP)/include/a02yyuw_parser.h $(COMP)/include/a02yyuw_filter.h
	$(CC) $(CFLAGS) -I$(COMP)/include -o $@ $(SRCS)

run: test_parser
	./test_parser

clean:
	rm -f test_parser
