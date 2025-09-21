.SUFFIXES:

PROGNAME    =  sandbartray
CC          =  cc
RM          =  rm -f
PKG_CONFIG  ?= pkg-config
PREFIX      ?= /usr/local
CFLAGS      ?= -O2 -Wall -Wextra -g
SUPPRESS    =  -Wno-missing-field-initializers -Wno-unused-parameter
DEPINCLUDES =  `$(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 \
               gdk-pixbuf-2.0 gtk4 gtk4-layer-shell-0`
DEPLIBS     =  `$(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0 \
               gdk-pixbuf-2.0 gtk4 gtk4-layer-shell-0`
OBJS        =  sandbartray.o snwatcher.o snhost.o snitem.o sndbusmenu.o

all: $(PROGNAME)

clean:
	$(RM) $(PROGNAME) $(OBJS)

install: all
	install -Dm755 $(PROGNAME) $(PREFIX)/bin/$(PROGNAME)

$(PROGNAME): $(OBJS)
	$(CC) $(DEPLIBS) $(LDFLAGS) -o $(PROGNAME) $(OBJS)

.SUFFIXES: .c .o
.c.o:
	$(CC) -c $(DEPINCLUDES) $(CFLAGS) $(SUPPRESS) $<

sandbartray.o: sandbartray.c snhost.h
snwatcher.o: snwatcher.c snwatcher.h
snhost.o: snhost.c snhost.h snwatcher.h snitem.h
snitem.o: snitem.c snitem.h sndbusmenu.h
sndbusmenu.o: sndbusmenu.c sndbusmenu.h snitem.h
