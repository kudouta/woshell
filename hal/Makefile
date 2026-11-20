# hal - 機種の層 (入口のヘッダーと、機種ごとの実装)
# Linux で実行する:  make  (Windows では wsl make のように WSL から)
#   作るもの:
#     build/lib$(MACHINE).a  機種の実装 ($(MACHINE)/*.c, *.S) と、CPU の実装 ($(CPU)/*.c)
#     build/crt0.o           プログラムの入口 (リンクのときに先頭に置く。ライブラリーには入れない)
#     build/boot.bin         フロッピーの起動ブロック (プログラムを LOAD_ADDR に読み込む)
#   使う側は include/ と $(MACHINE)/ をインクルードの場所に入れ、$(MACHINE)/stage2.ld でリンクする
#   MACHINE=se30  機種 (今は se30 だけ)
#   CPU=m68030    CPU (MMU など、CPU ごとの部分。今は m68030 だけ)

MACHINE ?= se30
CPU     ?= m68030
LIBC    ?= ../libc

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
AR      := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy

LOAD_ADDR ?= 0x00080000

M := $(MACHINE)
B := build

# -g: 行番号や型の情報を付ける (使う側の ELF で addr2line, objdump -S に使える)
CFLAGS  := -m68030 -m68881 -O2 -g -std=gnu11 -Wall -Wextra \
           -ffreestanding -fno-pic -fno-pie \
           -fomit-frame-pointer -fno-asynchronous-unwind-tables \
           -Iinclude -I$(M) -I$(LIBC)/src $(EXTRA_CFLAGS)
ASFLAGS := -m68030 -g -DLOAD_ADDR=$(LOAD_ADDR)

MACHINE_OBJS := $(addprefix $(B)/$(MACHINE)/,vectors.o trapent.o trap.o platform.o via.o irq.o video.o \
                adb.o keyboard.o asc.o beep.o romicon.o ramscan.o serial.o rtc.o)
CPU_OBJS     := $(addprefix $(B)/$(CPU)/,mmu.o)
HEADERS := $(wildcard include/*.h $(M)/*.h $(LIBC)/src/*.h)

all: lib

lib: $(B)/lib$(MACHINE).a $(B)/crt0.o $(B)/boot.bin

$(B)/lib$(MACHINE).a: $(MACHINE_OBJS) $(CPU_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(B)/$(CPU)/%.o: $(CPU)/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/$(MACHINE)/%.o: $(M)/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/$(MACHINE)/%.o: $(M)/%.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(B)/crt0.o: $(M)/crt0.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(B)/boot.elf: $(M)/boot.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c -o $(B)/boot.o $<
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-Ttext=0 -Wl,-e,_boot -o $@ $(B)/boot.o

$(B)/boot.bin: $(B)/boot.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(B)

.PHONY: all lib clean
