# Copyright (c) 2021 Greg Becker.  All rights reserved.

PROG := lftest

HDR	:= lfstack.h
SRC	:= lfstack.c main.c
OBJ := ${SRC:.c=.o}

LDLIBS := -pthread

PROG_VERSION := $(shell git describe --abbrev=8 --dirty --always --tags)
PLATFORM	:= $(shell uname -s | tr 'a-z' 'A-Z')

INCLUDE 	:= -I.
CDEFS 		:= -DPROG_VERSION=\"${PROG_VERSION}\"
CDEFS 		+= -DNDEBUG
CPPFLAGS	:= ${CDEFS}

CFLAGS		+= -std=c11 -Wall -O2 -g ${INCLUDE}
CFLAGS		+= -march=native
DEBUG		:= -O0 -DDEBUG -UNDEBUG -fno-omit-frame-pointer

# Always delete partially built targets.
#
.DELETE_ON_ERROR:
.NOTPARALLEL:

.PHONY:	all asan clean clobber debug distclean maintainer-clean


all: ${PROG}

asan: CFLAGS += ${DEBUG}
asan: CFLAGS += -fsanitize=address -fsanitize=undefined
asan: LDLIBS += -fsanitize=address -fsanitize=undefined
asan: ${PROG}

clean:
	rm -f ${PROG} ${OBJ} *.core
	rm -f $(patsubst %.c,.%.d,${SRC})

cleandir clobber distclean maintainer-clean: clean

debug: CFLAGS += ${DEBUG}
debug: ${PROG}

# Use gmake's link rule to produce the target.
#
${PROG}: ${OBJ}
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@


# We make ${OBJ} depend on the makefile so that all objects are rebuilt
# if the makefile changes.
#
${OBJ}: GNUmakefile

# Automatically generate/maintain dependency files.
#
.%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -M $(CPPFLAGS) ${INCLUDE} $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(patsubst %.c,.%.d,${SRC})
