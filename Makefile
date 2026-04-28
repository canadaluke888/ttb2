CC = gcc
CFLAGS = -Wall -pthread -Iinclude -Isrc/ui
LDFLAGS = -pthread -lncursesw -lpanelw -lsqlite3 -ljson-c -lz -lm
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
DOCDIR ?= $(DATADIR)/doc/ttb2
ICONDIR ?= $(DATADIR)/pixmaps
DESTDIR ?=
VERSION ?= $(shell cat VERSION)
DEB_ARCH := $(shell command -v dpkg >/dev/null 2>&1 && dpkg --print-architecture || uname -m)
DEB_STAGE := /tmp/ttb2-deb-stage
DEB_CONTROL := $(DEB_STAGE)/DEBIAN/control
DEB_PACKAGE := ttb2_$(VERSION)_$(DEB_ARCH).deb
CMAKE_BUILD_DIR ?= build/cmake
CMAKE_GENERATOR ?=
CMAKE_BUILD_TYPE ?= Release

OBJ_DIR = build/obj
BIN_DIR = build
OUT = $(BIN_DIR)/ttb2

SRC = $(shell find src -name '*.c' | sort)
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
	mkdir -p $(DESTDIR)$(ICONDIR)
	install -m 755 $(OUT) $(DESTDIR)$(BINDIR)/ttb2
	install -m 644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -m 644 LICENSE $(DESTDIR)$(DOCDIR)/LICENSE
	install -m 644 assets/icons/ttb2_icon_16.png $(DESTDIR)$(ICONDIR)/ttb2_icon_16.png
	install -m 644 assets/icons/ttb2_icon_32.png $(DESTDIR)$(ICONDIR)/ttb2_icon_32.png
	install -m 644 assets/icons/ttb2_icon_48.png $(DESTDIR)$(ICONDIR)/ttb2_icon_48.png
	install -m 644 assets/icons/ttb2_icon_64.png $(DESTDIR)$(ICONDIR)/ttb2_icon_64.png
	install -m 644 assets/icons/ttb2_icon_128.png $(DESTDIR)$(ICONDIR)/ttb2_icon_128.png
	install -m 644 assets/icons/ttb2_icon_256.png $(DESTDIR)$(ICONDIR)/ttb2_icon_256.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ttb2
	rm -f $(DESTDIR)$(DOCDIR)/README.md
	rm -f $(DESTDIR)$(DOCDIR)/LICENSE
	rm -f $(DESTDIR)$(ICONDIR)/ttb2_icon_16.png
	rm -f $(DESTDIR)$(ICONDIR)/ttb2_icon_32.png
	rm -f $(DESTDIR)$(ICONDIR)/ttb2_icon_48.png
	rm -f $(DESTDIR)$(ICONDIR)/ttb2_icon_64.png
	rm -f $(DESTDIR)$(ICONDIR)/ttb2_icon_128.png
	rm -f $(DESTDIR)$(ICONDIR)/ttb2_icon_256.png
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

cmake-configure:
	cmake -S . -B $(CMAKE_BUILD_DIR) $(if $(CMAKE_GENERATOR),-G "$(CMAKE_GENERATOR)",) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

cmake-build: cmake-configure
	cmake --build $(CMAKE_BUILD_DIR)

cmake-test: cmake-build
	ctest --test-dir $(CMAKE_BUILD_DIR) --output-on-failure

macos: cmake-build

windows-msys2: cmake-build

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)

.PHONY: run install uninstall deb cmake-configure cmake-build cmake-test macos windows-msys2 clean history_test table_index_test bookdb_semantic_test settings_test ui_dialog_list_test

history_test: tests/ui_history_test.c src/table.c src/table_ops.c src/ui/ui_data/ui_history.c include/ui/ui_history.h
	mkdir -p $(BIN_DIR)
	$(CC) -Wall -Iinclude -Isrc/ui -o $(BIN_DIR)/history_test tests/ui_history_test.c src/table.c src/table_ops.c src/ui/ui_data/ui_history.c

table_index_test: tests/table_index_test.c src/vector/table_index.c src/table.c src/table_ops.c src/db/book_db.c src/io/ttb_io.c
	mkdir -p $(BIN_DIR)
	$(CC) -Wall -Iinclude -Isrc/ui -o $(BIN_DIR)/table_index_test tests/table_index_test.c src/vector/table_index.c src/table.c src/table_ops.c src/db/book_db.c src/io/ttb_io.c -lsqlite3 -ljson-c -lz -lm

bookdb_semantic_test: tests/bookdb_semantic_test.c src/vector/table_index.c src/table.c src/table_ops.c src/db/book_db.c src/io/ttb_io.c
	mkdir -p $(BIN_DIR)
	$(CC) -Wall -Iinclude -Isrc/ui -o $(BIN_DIR)/bookdb_semantic_test tests/bookdb_semantic_test.c src/vector/table_index.c src/table.c src/table_ops.c src/db/book_db.c src/io/ttb_io.c -lsqlite3 -ljson-c -lz -lm

settings_test: tests/settings_test.c src/settings.c
	mkdir -p $(BIN_DIR)
	$(CC) -Wall -Iinclude -Isrc/ui -o $(BIN_DIR)/settings_test tests/settings_test.c src/settings.c -ljson-c

ui_dialog_list_test: tests/ui_dialog_list_test.c src/ui/ui_dialog/ui_dialog_common.c
	mkdir -p $(BIN_DIR)
	$(CC) -Wall -Iinclude -Isrc/ui -o $(BIN_DIR)/ui_dialog_list_test tests/ui_dialog_list_test.c src/ui/ui_dialog/ui_dialog_common.c -lncursesw -lpanelw
