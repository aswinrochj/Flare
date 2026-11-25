# Host-side unit tests for the hardware-independent firmware modules.
#   make -C firmware/test        # build and run all tests
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O1 -g
PROTO    = ../components/safenode_proto
NODE     = ../node/v1.0/main
GW       = ../gateway/main
INC      = -I$(PROTO)/include -I$(NODE) -I$(GW)
VECTORS  = -DVECTORS_PATH=\"$(abspath ../../test/vectors/frames.txt)\"

TESTS = test_proto test_classifier test_node_table

all: run

test_proto: test_proto.c $(PROTO)/sn_proto.c
	$(CC) $(CFLAGS) $(INC) $(VECTORS) -o $@ $^

test_classifier: test_classifier.c $(NODE)/classifier.c $(PROTO)/sn_proto.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

test_node_table: test_node_table.c $(GW)/node_table.c $(PROTO)/sn_proto.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

run: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
