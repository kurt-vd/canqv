PROGRAMS = canspy

default: $(PROGRAMS)

VERSION	:= $(shell git describe --tags --always --dirty)
CFLAGS	= -Wall -O0 -g3

-include config.mk

CPPFLAGS += -DVERSION=\"$(VERSION)\"

clean:
	rm -f $(PROGRAMS)

