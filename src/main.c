/**
 * @file main.c
 * @brief woshell: C の式に似た書き方のシェル (画面は VTermLib の VT102 相当の端末)
 */
#include "platform.h"
#include "console.h"
#include "keyboard.h"
#include "serial.h"
#include "fmt.h"
#include "shell.h"
#include "cmds.h"
#include "symtab.h"
#include "trap.h"
#include "beep.h"
#include "vt.h"

#define SERIAL_PORT_DEFAULT (SERIAL_MODEM) /* 起動したときのポート (変えるには port コマンド) */
#define SERIAL_BAUD_DEFAULT (9600)      /* 起動したときの速さ (変えるには baud コマンド) */

/* 起動のアイコン (32 x 32) の位置 (文字の領域の左上からのドット) と、起動の文を始める桁 (1 から)。
   文は 3 行 (12 ドット x 3 = 36 ドット) なので、上下に 2 ドットずつ空けて置く */
#define ICON_X        (2)
#define ICON_Y        (2)
#define ICON_TEXT_COL "8"


/* 構えていないとき (シェルの外) に例外が起きた: 知らせて止まる */
void trap_fatal(const struct trap_info *info)
{
    printf("\nfatal exception: %s (vector %d) at pc 0x%08x",
           trap_name(info->vector), info->vector, (unsigned)info->pc);
    const struct symtab_entry *s = shell_sym_near(info->pc);
    if (s)
        printf(" (%s+0x%x)", s->name, (unsigned)(info->pc - s->addr));
    if (info->has_addr)
        printf(", address 0x%08x", (unsigned)info->addr);
    printf("\nsystem halted.\n");
    console_flush();
}

int main(void)
{
    platform_init();
    console_init();
    keyboard_init();
    beep_init();
    serial_init(SERIAL_PORT_DEFAULT, SERIAL_BAUD_DEFAULT);

    /* 起動の文。ROM の起動のアイコンが読めたら、左に置いて文をその右に並べる (Linux の起動のペンギンのように)。
       文を先に書き、後から絵を重ねる (絵のある行に後から書くと、その行の絵が消えるため) */
    uint32_t icon[32];
    const int has_icon = platform_startup_icon(icon);
    const char *indent = has_icon ? "\033[" ICON_TEXT_COL "G" : "";
    printf("%s", indent);
    cmd_version();
    printf("%sRAM %u KB, %d symbols. Serial: %s port %u bps 8N1.\n", indent,
           (unsigned)(platform_ram_size() >> 10), symtab_count,
           serial_port() == SERIAL_MODEM ? "modem" : "printer", (unsigned)serial_baud());
    printf("%sType help for commands.\n", indent);
    if (has_icon)
        vt_draw_bitmap(ICON_X, ICON_Y, 32, 32, icon);
    printf("\n");
    shell_run();
}
