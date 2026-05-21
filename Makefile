SHELL := /bin/sh

SHELF ?= ../coco-shelf

CMOC ?= $(if $(wildcard $(SHELF)/bin/cmoc),$(SHELF)/bin/cmoc,cmoc)
LWASM_ORIG ?= $(if $(wildcard $(SHELF)/bin/lwasm.orig),$(SHELF)/bin/lwasm.orig,lwasm)
DCCMOC_SED ?= $(SHELF)/CoCoC/Source/Libs/KLibc/dccmoc.sed
LEGACY_DEFS ?= $(SHELF)/CoCoC/Defs
CMOC_OS9_INCLUDE ?= $(SHELF)/cmoc_os9/include
CMOC_OS9_LIB ?= $(SHELF)/cmoc_os9/lib

TARGET ?= te_cmoc
TRANSLATED_C := te.cmoc.c

CMOCFLAGS := --intermediate --lwasm=$(LWASM_ORIG) \
	-Dfgetc=getc -Dfputc=putc \
	-I$(LEGACY_DEFS) -I$(CMOC_OS9_INCLUDE) \
	--os9 -nodefaultlibs
LDFLAGS_CMOC := -L$(CMOC_OS9_LIB) -lc

.PHONY: all clean

all: $(TARGET)

$(TRANSLATED_C): te.c $(DCCMOC_SED)
	sed -f $(DCCMOC_SED) $< > $@

$(TARGET): $(TRANSLATED_C) sgstat.h
	$(CMOC) $(CMOCFLAGS) -o$@ $(TRANSLATED_C) $(LDFLAGS_CMOC)

clean:
	rm -f $(TRANSLATED_C) te.cmoc.lst te.cmoc.o te.cmoc.s \
		$(TARGET) $(TARGET).link $(TARGET).map
