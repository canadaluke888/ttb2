CC = gcc
CFLAGS = -Wall -Iinclude -Isrc/ui
LDFLAGS = -lncursesw -lpanelw -lsqlite3 -ljson-c -lz
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
DOCDIR ?= $(DATADIR)/doc/ttb2
ICONDIR ?= $(DATADIR)/pixmaps
DESTDIR ?=
VERSION ?= $(shell cat VERSION)
DEB_ARCH := $(shell dpkg --print-architecture)
DEB_STAGE := /tmp/ttb2-deb-stage
DEB_CONTROL := $(DEB_STAGE)/DEBIAN/control
DEB_PACKAGE := ttb2_$(VERSION)_$(DEB_ARCH).deb

SRC_DIRS = src src/db src/io src/ui
OBJ_DIR = build/obj
BIN_DIR = build
OUT = $(BIN_DIR)/ttb2

SRC = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJ = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC))

all: $(OUT)

$(OUT): $(OBJ)
	mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run:
	./build/ttb2

install: $(OUT)
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(DOCDIR)
	mkdir -p $(DESTDIR)$(DOCDIR)/assets
	mkdir -p $(DESTDIR)$(ICONDIR)
	install -m 755 $(OUT) $(DESTDIR)$(BINDIR)/ttb2
	install -m 644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -m 644 LICENSE $(DESTDIR)$(DOCDIR)/LICENSE
	install -m 644 assets/ttb2_img.png $(DESTDIR)$(ICONDIR)/ttb2.png
	install -m 644 assets/main_ui.png $(DESTDIR)$(DOCDIR)/assets/main_ui.png
	install -m 644 assets/menu.png $(DESTDIR)$(DOCDIR)/assets/menu.png
	install -m 644 assets/ttb2_img.png $(DESTDIR)$(DOCDIR)/assets/ttb2_img.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ttb2
	rm -f $(DESTDIR)$(DOCDIR)/README.md
	rm -f $(DESTDIR)$(DOCDIR)/LICENSE
	rm -f $(DESTDIR)$(DOCDIR)/assets/main_ui.png
	rm -f $(DESTDIR)$(DOCDIR)/assets/menu.png
	rm -f $(DESTDIR)$(DOCDIR)/assets/ttb2_img.png
	rm -f $(DESTDIR)$(ICONDIR)/ttb2.png
	rmdir $(DESTDIR)$(DOCDIR)/assets 2>/dev/null || true
	rmdir $(DESTDIR)$(DOCDIR) 2>/dev/null || true

deb: $(OUT)
	rm -rf $(DEB_STAGE)
	mkdir -p $(DEB_STAGE)/DEBIAN
	$(MAKE) install PREFIX=/usr DESTDIR=$(DEB_STAGE)
	printf '%s\n' \
		'Package: ttb2' \
		'Version: $(VERSION)' \
		'Section: utils' \
		'Priority: optional' \
		'Architecture: $(DEB_ARCH)' \
		'Maintainer: Luke Canada <canadaluke888@gmail.com>' \
		'Depends: libc6, libncursesw6, libsqlite3-0, libjson-c5, zlib1g' \
		'Description: Terminal Table Builder 2.0' \
		' ncurses-based table editor with CSV, XLSX, PDF, TTBL, and TTBX support.' \
		> $(DEB_CONTROL)
	dpkg-deb --build $(DEB_STAGE) $(DEB_PACKAGE)

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)

.PHONY: run install uninstall deb clean seekdb_bench
seekdb_bench: tools/seekdb_bench.c src/db/seekdb.c include/seekdb.h
	mkdir -p $(BIN_DIR)
	$(CC) -O2 -Wall -Iinclude -o $(BIN_DIR)/seekdb_bench tools/seekdb_bench.c src/db/seekdb.c -lsqlite3
