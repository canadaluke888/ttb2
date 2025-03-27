CC = gcc
CFLAGS = -Wall -Iinclude
LDFLAGS = -lncursesw

SRC = src/main.c src/table.c src/ui.c
OUT = build/tablecraft

$(OUT): $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -rf build
