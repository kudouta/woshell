# VTermLib - VT102 相当の文字端末のライブラリー (機種に依存しない。機種の層 hal の入口だけを使う)
# Linux で実行する:  make  (Windows では wsl make のように WSL から)
#   作るもの:
#     build/libvterm.a     端末 (vt.h)。フォントもここに入る
#     build/vterm1440.img  デモ (demo/main.c。hal の lib$(MACHINE).a、libc の libc.a ともリンクする)
#   make lib        ライブラリーだけを作る
#   make test       PC で端末の中身 (vtcore.c) を確かめる
#   FONT=...        フォントのファイル (テキスト)。大きさが変わると画面の桁数と行数も変わる
#   HAL=...         機種の層の場所 (標準は ../hal)、LIBC=... C ライブラリーの場所 (標準は ../libc)、MACHINE=se30

MACHINE ?= se30
HAL     ?= ../hal
LIBC    ?= ../libc
FONT    ?= assets/font/vt6x12.txt

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
AR      := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
HOSTCC  ?= gcc

LOAD_ADDR ?= 0x00080000

B := build

CFLAGS  := -m68030 -m68881 -O2 -g -std=gnu11 -Wall -Wextra \
           -ffreestanding -fno-pic -fno-pie \
           -fomit-frame-pointer -fno-asynchronous-unwind-tables \
           -Isrc -I$(HAL)/include -I$(HAL)/$(MACHINE) -I$(LIBC)/src -I$(B) $(EXTRA_CFLAGS)
LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
           -Wl,--defsym=LOAD_ADDR=$(LOAD_ADDR)

VTERM_OBJS := $(addprefix $(B)/,vt.o vtcore.o vtdraw.o)
DEMO_OBJS  := $(B)/demo/main.o
HEADERS := $(wildcard src/*.h $(HAL)/include/*.h $(HAL)/$(MACHINE)/*.h $(LIBC)/src/*.h) $(B)/font.h

# 機種の層 (hal) と C ライブラリー (libc) が作るもの
HAL_OUT  := $(HAL)/build/lib$(MACHINE).a $(HAL)/build/crt0.o $(HAL)/build/boot.bin
LIBC_OUT := $(LIBC)/build/libc.a

all: lib $(B)/vterm1440.img $(B)/stage2.lst

lib: $(B)/libvterm.a

$(B)/libvterm.a: $(VTERM_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

# 文字 (フォントのファイルを書き換えると反映される。FONT を変えたときも作り直す)
$(B)/font.h: tools/common/vtfont.py $(FONT) $(B)/font.name
	@mkdir -p $(@D)
	python3 -I tools/common/vtfont.py $(FONT) $@

# 使ったフォントの名前を覚えておき、変わったら font.h を作り直す
$(B)/font.name: FORCE
	@mkdir -p $(@D)
	@echo '$(FONT)' | cmp -s - $@ || echo '$(FONT)' > $@

$(B)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/demo/%.o: demo/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- デモ ----
# いつも hal の make を呼ぶ。中で何も変わらなければ、日付は変わらず、作り直しも起きない
$(HAL_OUT) &: FORCE
	$(MAKE) -C $(HAL) lib MACHINE=$(MACHINE) LOAD_ADDR=$(LOAD_ADDR)

$(LIBC_OUT): FORCE
	$(MAKE) -C $(LIBC) lib

# libvterm は lib$(MACHINE) を、どれも libc を使うので、この順に並べる
# (libc.a はクロスコンパイラーの Linux 用の libc.a と取り違えないよう、ファイルの場所で指定する)
$(B)/stage2.elf: $(DEMO_OBJS) $(B)/libvterm.a $(HAL_OUT) $(LIBC_OUT) $(HAL)/$(MACHINE)/stage2.ld
	$(CC) $(LDFLAGS) -T $(HAL)/$(MACHINE)/stage2.ld -o $@ $(HAL)/build/crt0.o $(DEMO_OBJS) \
	    -L$(B) -L$(HAL)/build -lvterm -l$(MACHINE) $(LIBC_OUT) -lgcc

$(B)/stage2.bin: $(B)/stage2.elf
	$(OBJCOPY) -O binary $< $@

$(B)/stage2.lst: $(B)/stage2.elf
	$(OBJDUMP) -d -S $< > $@

# 1.44MB のフロッピー
$(B)/vterm1440.img: $(HAL)/build/boot.bin $(B)/stage2.bin $(HAL)/tools/$(MACHINE)/mkfloppy.py
	python3 -I $(HAL)/tools/$(MACHINE)/mkfloppy.py $(HAL)/build/boot.bin $(B)/stage2.bin $@ 1440

# ---- PC で確かめる (端末の中身だけ。画面は描かない) ----
$(B)/host/test_vt: tests/test_vt.c src/vtcore.c src/vtcore.h $(B)/font.h
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -g -std=gnu11 -Wall -Wextra -Isrc -I$(HAL)/include -I$(HAL)/$(MACHINE) -I$(LIBC)/src -I$(B) \
	    -o $@ tests/test_vt.c src/vtcore.c

test: $(B)/host/test_vt
	$(B)/host/test_vt

clean:
	rm -rf $(B)

.PHONY: all lib clean test FORCE
