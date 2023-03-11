BINS = sandbar

PREFIX ?= /usr/local
CFLAGS += -Wall -Wextra -Wno-unused-parameter -g

all: $(BINS)

clean:
	$(RM) $(BINS) $(addsuffix .o,$(BINS))

install: all
	install -D -t $(PREFIX)/bin $(BINS)

WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.o: xdg-shell-protocol.h

xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
xdg-output-unstable-v1-protocol.o: xdg-output-unstable-v1-protocol.h

wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.o: wlr-layer-shell-unstable-v1-protocol.h

river-status-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/river-status-unstable-v1.xml $@
river-status-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/river-status-unstable-v1.xml $@
river-status-unstable-v1-protocol.o: river-status-unstable-v1-protocol.h

river-control-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/river-control-unstable-v1.xml $@
river-control-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/river-control-unstable-v1.xml $@
river-control-unstable-v1-protocol.o: river-control-unstable-v1-protocol.h

sandbar.o: utf8.h xdg-shell-protocol.h xdg-output-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h river-status-unstable-v1-protocol.h river-control-unstable-v1-protocol.h 

# Protocol dependencies
sandbar: xdg-shell-protocol.o xdg-output-unstable-v1-protocol.o wlr-layer-shell-unstable-v1-protocol.o river-status-unstable-v1-protocol.o river-control-unstable-v1-protocol.o

# Library dependencies
sandbar: CFLAGS+=$(shell pkg-config --cflags wayland-client wayland-cursor fcft pixman-1)
sandbar: LDLIBS+=$(shell pkg-config --libs wayland-client wayland-cursor fcft pixman-1) -lrt

.PHONY: all clean install
