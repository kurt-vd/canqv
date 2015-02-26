PROGRAMS = canqv

default: $(PROGRAMS)

VERSION	:= $(shell git describe --tags --always --dirty)
CFLAGS	= -Wall -O0 -g3
CPPFLAGS= -D_GNU_SOURCE
PREFIX= /usr/local

-include config.mk

CPPFLAGS += -DVERSION=\"$(VERSION)\"

clean:
	rm -f $(PROGRAMS)

install: canqv
	install -v canqv $(DESTDIR)$(PREFIX)/bin

