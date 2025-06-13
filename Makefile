CC = gcc
PYTHON_CONFIG = python3.12-config
CFLAGS = -Wall -Iinclude $(shell $(PYTHON_CONFIG) --includes)
LDFLAGS = -lncursesw $(shell $(PYTHON_CONFIG) --ldflags) -lpython3.12

SRC_DIR = src
OBJ_DIR = build/obj
BIN_DIR = build
OUT = $(BIN_DIR)/tablecraft

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

all: $(OUT)

$(OUT): $(OBJ)
	mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)
