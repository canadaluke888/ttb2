CC = gcc
CFLAGS = -Wall -Iinclude -Isrc/ui
LDFLAGS = -lncursesw -lpanelw -lsqlite3 -ljson-c -lz
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=
VERSION ?= $(shell cat VERSION)
DEB_ARCH := $(shell dpkg --print-architecture)
DEB_STAGE := /tmp/ttb2-deb-stage
DEB_CONTROL := $(DEB_STAGE)/DEBIAN/control
DEB_PACKAGE := ttb2_$(VERSION)_$(DEB_ARCH).deb

SRC_DIRS = src src/db src/parser src/ui
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
	install -m 755 $(OUT) $(DESTDIR)$(BINDIR)/ttb2

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ttb2

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
		'Depends: libc6, libncursesw6, libpanelw6, libsqlite3-0, libjson-c5, zlib1g' \
		'Description: Terminal Table Builder 2.0' \
		' ncurses-based table editor with CSV, XLSX, TTBL, and TTBX support.' \
		> $(DEB_CONTROL)
	dpkg-deb --build $(DEB_STAGE) $(DEB_PACKAGE)

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)

.PHONY: run install uninstall deb clean seekdb_bench
seekdb_bench: tools/seekdb_bench.c src/seekdb.c include/seekdb.h
	mkdir -p $(BIN_DIR)
	$(CC) -O2 -Wall -Iinclude -o $(BIN_DIR)/seekdb_bench tools/seekdb_bench.c src/seekdb.c -lsqlite3
