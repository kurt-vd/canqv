PROGRAMS = canspy

default: $(PROGRAMS)

VERSION	:= $(shell git describe --tags --always --dirty)
CFLAGS	= -Wall -O0 -g3
CPPFLAGS= -D_GNU_SOURCE

-include config.mk

CPPFLAGS += -DVERSION=\"$(VERSION)\"

clean:
	rm -f $(PROGRAMS)

