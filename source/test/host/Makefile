# Host unit tests for the pure-C parts of the a02yyuw, ha_mqtt and web_report components.
# Usage (from the project root):  make -C test/host run
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O0 -g
COMP    := ../../components/a02yyuw
HA      := ../../components/ha_mqtt
WEB     := ../../components/web_report

SRCS     := test_parser.c $(COMP)/a02yyuw_parser.c $(COMP)/a02yyuw_filter.c
HA_SRCS  := test_ha_payload.c $(HA)/ha_payload.c
WEB_SRCS := test_web_payload.c $(WEB)/web_payload.c

.PHONY: all run clean

all: test_parser test_ha_payload test_web_payload

test_parser: $(SRCS) $(COMP)/include/a02yyuw_parser.h $(COMP)/include/a02yyuw_filter.h
	$(CC) $(CFLAGS) -I$(COMP)/include -o $@ $(SRCS)

test_ha_payload: $(HA_SRCS) $(HA)/include/ha_payload.h
	$(CC) $(CFLAGS) -I$(HA)/include -o $@ $(HA_SRCS)

test_web_payload: $(WEB_SRCS) $(WEB)/include/web_payload.h
	$(CC) $(CFLAGS) -I$(WEB)/include -o $@ $(WEB_SRCS)

run: test_parser test_ha_payload test_web_payload
	./test_parser
	./test_ha_payload
	./test_web_payload

clean:
	rm -f test_parser test_ha_payload test_web_payload
