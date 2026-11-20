# libc - 自前の小さな C ライブラリー (機種に依存しない)
# Linux で実行する:  make  (Windows では wsl make のように WSL から)
#   作るもの: build/libc.a  (memcpy / memset と printf / sprintf)
#   使う側は src/ をインクルードの場所に入れ、build/libc.a をライブラリーの最後 (-lgcc の前) に並べる。
#   クロスコンパイラーにも同じ名前の libc.a (Linux 用) があるので、-lc ではなくファイルの場所で指定する

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
AR      := $(CROSS)ar

B := build

CFLAGS  := -m68030 -m68881 -O2 -g -std=gnu11 -Wall -Wextra \
           -ffreestanding -fno-pic -fno-pie \
           -fomit-frame-pointer -fno-asynchronous-unwind-tables \
           -Isrc $(EXTRA_CFLAGS)

OBJS    := $(addprefix $(B)/,mem.o fmt.o)
HEADERS := $(wildcard src/*.h)

all: lib

lib: $(B)/libc.a

$(B)/libc.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(B)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(B)

.PHONY: all lib clean
