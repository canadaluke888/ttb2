CC = gcc
CFLAGS = -Wall -Iinclude -Isrc/ui
LDFLAGS = -lncursesw -lpanelw -lsqlite3 -ljson-c -lz

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

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)

.PHONY: seekdb_bench
seekdb_bench: tools/seekdb_bench.c src/seekdb.c include/seekdb.h
	mkdir -p $(BIN_DIR)
	$(CC) -O2 -Wall -Iinclude -o $(BIN_DIR)/seekdb_bench tools/seekdb_bench.c src/seekdb.c -lsqlite3
