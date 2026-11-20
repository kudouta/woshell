# woshell - C の式に似た書き方のシェル (SE/30)
# Linux で実行する:  make  (Windows では wsl make のように WSL から)
#   hal/       機種のレイヤー (インターフェースのヘッダーと SE/30 の実装)  → hal/build/libse30.a, crt0.o, boot.bin
#   libc/      自前の小さな C ライブラリー (memcpy, printf)                → libc/build/libc.a
#   vtermlib/  VT102 相当の端末 (機種に依存しない)                         → vtermlib/build/libvterm.a
#   src/       シェル                                                      → build/woshell1440.img (1.44MB のフロッピー)
#   MACHINE=se30  機種 (hal/<機種>)。今は se30 だけ
#   FONT=...      端末の文字 (vtermlib/assets/font/ の書き方)。vtermlib のフォルダーからの位置で書く
#   make test     端末の中身を PC で確認する

MACHINE ?= se30

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
NM      := $(CROSS)nm

LOAD_ADDR ?= 0x00080000

# 版 (VERSION ファイル) と、git describe の結果 (git がなければ空。タグと同じなら省く)
VERSION  := $(shell cat VERSION)
GIT_DESC := $(filter-out v$(VERSION),$(shell git describe --tags --always --dirty 2>/dev/null))

HAL  := hal
VT   := vtermlib
LIBC := libc
B    := build

# -g: ELF (build/stage2.elf) に行番号や型の情報を付ける (gdb, addr2line, objdump -S 用)。
#     フロッピーに書く stage2.bin は objcopy で情報を落とすので変わらない
CFLAGS  := -m68030 -m68881 -O2 -g -std=gnu11 -Wall -Wextra \
           -ffreestanding -fno-pic -fno-pie \
           -fomit-frame-pointer -fno-asynchronous-unwind-tables \
           -Isrc -I$(VT)/src -I$(VT)/build -I$(HAL)/include -I$(HAL)/$(MACHINE) -I$(LIBC)/src -I$(B) $(EXTRA_CFLAGS)
LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
           -Wl,--defsym=LOAD_ADDR=$(LOAD_ADDR)

OBJS    := $(addprefix $(B)/obj/,main.o console.o shell.o cmds.o)
HEADERS := $(wildcard src/*.h $(VT)/src/*.h $(HAL)/include/*.h $(HAL)/$(MACHINE)/*.h $(LIBC)/src/*.h)

# 下位のレイヤーが作るもの
HAL_OUT  := $(HAL)/build/lib$(MACHINE).a $(HAL)/build/crt0.o $(HAL)/build/boot.bin
VT_OUT   := $(VT)/build/libvterm.a
LIBC_OUT := $(LIBC)/build/libc.a
CRT0     := $(HAL)/build/crt0.o
# libc.a はクロスコンパイラーの Linux 用の libc.a と取り違えないよう、-lc ではなくパスで指定する
LIBS     := -L$(VT)/build -L$(HAL)/build -lvterm -l$(MACHINE) $(LIBC_OUT) -lgcc
LD_SCRIPT := $(HAL)/$(MACHINE)/stage2.ld

all: $(B)/woshell1440.img $(B)/stage2.lst

# ---- 下位のレイヤー ----
# いつも下位のレイヤーの make を呼ぶ。中で何も変わらなければ、日付は変わらず、作り直しも起きない
$(HAL_OUT) &: FORCE
	$(MAKE) -C $(HAL) lib MACHINE=$(MACHINE) LOAD_ADDR=$(LOAD_ADDR)

$(VT_OUT) &: FORCE
	$(MAKE) -C $(VT) lib MACHINE=$(MACHINE) $(if $(FONT),FONT=$(FONT))

$(LIBC_OUT): FORCE
	$(MAKE) -C $(LIBC) lib

# ---- 組み立て ----
# 端末ライブラリーを先に作る (vtermlib/build にできる font.h を使うため)
$(B)/obj/%.o: src/%.c $(HEADERS) | $(VT_OUT)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# 版の値を build/version.h に書く。中身が変わったときだけ書き換える (変わらなければ作り直しは起きない)
$(B)/version.h: FORCE
	@mkdir -p $(@D)
	@printf '#define WOSHELL_VERSION "%s"\n#define WOSHELL_GIT "%s"\n' '$(VERSION)' '$(GIT_DESC)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(B)/obj/cmds.o: $(B)/version.h

# ---- シンボルテーブル (関数や変数を名前で呼ぶため) ----
# 表を入れると大きさが変わってアドレスがずれるので、3 回リンクする:
#   1. 空のテーブルでリンクし、シンボルの名前を集める
#   2. 1 の名前で表を作ってリンクする (表の大きさは最終と同じになる。アドレスはまだ 1 のもの)
#   3. 2 のアドレスで表を作ってリンクする。2 と 3 のアドレスが同じことを確認する
# crt0.o (エントリーポイント) を先頭に置き、ライブラリーは最後に並べる (libvterm は lib$(MACHINE) を、どれも libc を使う)
LINK = $(CC) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $(CRT0) $(OBJS) $(filter $(B)/sym/%.o,$^) $(LIBS)

$(B)/sym/symtab_a.c: tools/common/mksymtab.py
	@mkdir -p $(@D)
	python3 -I tools/common/mksymtab.py $@
$(B)/stage2_a.elf: $(OBJS) $(B)/sym/symtab_a.o $(HAL_OUT) $(VT_OUT) $(LIBC_OUT) $(LD_SCRIPT)
	$(LINK)
$(B)/sym/stage2_b.c: $(B)/stage2_a.elf tools/common/mksymtab.py
	$(NM) $< > $(B)/syms_a.txt
	python3 -I tools/common/mksymtab.py $(B)/syms_a.txt $@
$(B)/stage2_b.elf: $(OBJS) $(B)/sym/stage2_b.o $(HAL_OUT) $(VT_OUT) $(LIBC_OUT) $(LD_SCRIPT)
	$(LINK)
$(B)/sym/symtab.c: $(B)/stage2_b.elf tools/common/mksymtab.py
	$(NM) $< > $(B)/syms_b.txt
	python3 -I tools/common/mksymtab.py $(B)/syms_b.txt $@
$(B)/stage2.elf: $(OBJS) $(B)/sym/symtab.o $(HAL_OUT) $(VT_OUT) $(LIBC_OUT) $(LD_SCRIPT)
	$(LINK)
	$(NM) $@ > $(B)/syms.txt
	cmp -s $(B)/syms.txt $(B)/syms_b.txt || (echo "シンボルのアドレスが 2 回目と 3 回目で違う"; rm -f $@; exit 1)

$(B)/sym/%.o: $(B)/sym/%.c src/symtab.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/stage2.bin: $(B)/stage2.elf
	$(OBJCOPY) -O binary $< $@

$(B)/stage2.lst: $(B)/stage2.elf
	$(OBJDUMP) -d -S $< > $@

# 1.44MB のフロッピー
$(B)/woshell1440.img: $(HAL)/build/boot.bin $(B)/stage2.bin $(HAL)/tools/$(MACHINE)/mkfloppy.py
	python3 -I $(HAL)/tools/$(MACHINE)/mkfloppy.py $(HAL)/build/boot.bin $(B)/stage2.bin $@ 1440

# 端末の中身を PC で確認する
test:
	$(MAKE) -C $(VT) test MACHINE=$(MACHINE)

clean:
	rm -rf $(B)
	$(MAKE) -C $(HAL) clean
	$(MAKE) -C $(VT) clean
	$(MAKE) -C $(LIBC) clean

.PHONY: all clean test FORCE
