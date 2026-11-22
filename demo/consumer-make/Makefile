# Minimal shrewd-basic consumer with plain make: generate descriptors at
# build time, compile the library sources into the binary. Usage:
#   make -C demo/consumer-make run
#
# CC / PYTHON override the toolchain; all paths resolve relative to this
# Makefile, so any invocation directory works.

CC      ?= cc
PYTHON  ?= python3
HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../..)
BUILD   := $(HERE)build

GEN     := $(BUILD)/config_reflect
STUBS   := $(GEN).c $(GEN).h $(GEN)_json.c $(GEN)_json.h $(GEN)_schema_dump.c
LIB_SRC := $(wildcard $(ROOT)/library/*/*.c)
DEMO    := $(BUILD)/config_demo
DUMPER  := $(BUILD)/schema_dump
SCHEMA  := $(BUILD)/config_schema.json

CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/include/shrewd-basic-internal \
            -I$(HERE) -I$(BUILD)

.PHONY: all run schema clean

all: $(DEMO) $(SCHEMA)

$(STUBS): $(HERE)config.h $(ROOT)/tools/reflect_gen.py
	@mkdir -p $(BUILD)
	$(PYTHON) $(ROOT)/tools/reflect_gen.py $(HERE)config.h -o $(GEN) --quiet --schema-dump

$(DEMO): $(HERE)main.c $(STUBS) $(LIB_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(HERE)main.c $(GEN).c $(GEN)_json.c $(LIB_SRC) -o $@

$(DUMPER): $(GEN)_schema_dump.c $(STUBS) $(LIB_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GEN)_schema_dump.c $(GEN).c $(GEN)_json.c $(LIB_SRC) -o $@

$(SCHEMA): $(DUMPER)
	$(DUMPER) demo_config_t $@

schema: $(SCHEMA)

run: $(DEMO)
	$(DEMO)

clean:
	rm -rf $(BUILD)
