CC = gcc
CFLAGS = -Wall -Iinclude
LDFLAGS = -lncursesw

SRC = src/main.c src/table.c src/errors.c \
      src/ui_init.c src/ui_draw.c src/ui_edit.c src/ui_prompt.c src/ui_loop.c
OUT = build/tablecraft

$(OUT): $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -rf build
