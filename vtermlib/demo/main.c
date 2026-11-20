/**
 * @file main.c
 * @brief VTermLib のデモ (libvterm.a と libse30.a を使う側の例)
 *
 * メニューから項目を選ぶと、その機能を使う画面を出す。画面はすべてエスケープシーケンスで描く
 * (このプログラムが端末の「ホスト」の役をする)。
 */
#include "platform.h"
#include "keyboard.h"
#include "beep.h"
#include "trap.h"
#include "timer.h"
#include "video.h"
#include "vt.h"
#include "vtcore.h"                     /* 速さを測るときだけ、中身を直接呼ぶ */
#include "fmt.h"

#define ESC "\033"
#define CSI ESC "["

/* printf の出力先 (fmt.h): 端末へ */
void fmt_putc(int c)
{
    vt_putc(c);
}

/* 構えていないとき (このプログラムの中) に例外が起きた: 知らせて止まる */
void trap_fatal(const struct trap_info *info)
{
    printf(CSI "m\n\nfatal exception: %s (vector %d) at pc 0x%08x",
           trap_name(info->vector), info->vector, (unsigned)info->pc);
    if (info->has_addr)
        printf(", address 0x%08x", (unsigned)info->addr);
    printf("\nsystem halted.\n");
    vt_flush();
}

/* ---- 小さな道具 ---- */

/* 垂直同期 n 回分待つ (その間も画面に反映する) */
static void pause_frames(int n)
{
    while (n-- > 0) {
        video_wait_vsync();
        vt_idle();
    }
}

/* キーの残り (矢印などは複数のバイトになる) を捨てる */
static void drain(void)
{
    while (vt_getc() >= 0)
        ;
}

static int get_key(void)
{
    const int c = vt_getc_wait();
    pause_frames(1);
    drain();
    return c;
}

/* 一番下の行に案内を出してキーを待つ */
static void wait_key(void)
{
    printf(CSI "%d;1H" CSI "0;7m Press any key " CSI "m", vt_rows());
    get_key();
}

/* 画面を消して見出しを出す */
static void title(const char *s)
{
    printf(ESC "c");                    /* RIS: すべて初期状態に */
    printf(CSI "1;7m %s " CSI "m\n\n", s);
}

/* ---- 各項目 ---- */

static void page_attrs(void)
{
    title("Character attributes (SGR) and character sets");
    static const struct { const char *sgr, *name; } a[] = {
        { "0", "normal" }, { "1", "bold" }, { "4", "underline" }, { "5", "blink" },
        { "7", "reverse" }, { "1;4", "bold underline" }, { "1;7", "bold reverse" },
        { "4;7", "underline reverse" }, { "5;7", "blink reverse" }, { "1;4;5;7", "all four" },
    };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        char seq[16];
        sprintf(seq, "ESC[%sm", a[i].sgr);
        printf("  %-12s " CSI "%sm%-20s" CSI "m  " CSI "%smThe quick brown fox 0123" CSI "m\n",
               seq, a[i].sgr, a[i].name, a[i].sgr);
    }
    printf("  ESC[8m       invisible: [" CSI "8msecret" CSI "28m] (between the brackets)\n\n");
    vt_flush();

    printf("  ASCII 0x20-0x7E:\n  ");
    for (int c = 0x20; c < 0x7F; c++) {
        vt_putc(c);
        if (c == 0x4F)
            printf("\n  ");
    }
    printf("\n\n  DEC special graphics (ESC ( 0), 0x5F-0x7E:\n  ");
    for (int c = 0x5F; c < 0x7F; c++) {
        printf("%c" ESC "(0%c" ESC "(B ", c, c);
        if (c == 0x6E)
            printf("\n  ");
    }
    printf("\n  Bold: " CSI "1m" ESC "(0");
    for (int c = 0x5F; c < 0x7F; c++)
        printf("%c ", c);
    printf(ESC "(B" CSI "m\n\n  SO/SI with G1 = graphics (ESC ) 0): " ESC ")0" "lq\x0e" "lq\x0f" "lq   UK set (ESC ( A): #" ESC "(A#" ESC "(B\n");
    wait_key();
}

static void box(int top, int left, int h, int w)
{
    printf(ESC "(0" CSI "%d;%dHl", top, left);
    for (int i = 0; i < w - 2; i++)
        vt_putc('q');
    vt_putc('k');
    for (int r = 1; r < h - 1; r++)
        printf(CSI "%d;%dHx" CSI "%d;%dHx", top + r, left, top + r, left + w - 1);
    printf(CSI "%d;%dHm", top + h - 1, left);
    for (int i = 0; i < w - 2; i++)
        vt_putc('q');
    printf("j" ESC "(B");
}

static void page_lines(void)
{
    title("Line drawing (DEC special graphics)");
    box(4, 4, 8, 30);
    printf(CSI "6;8HA box drawn with" CSI "7;8H" ESC "(0lqkxmj" ESC "(B characters." CSI "9;8H" CSI "1mBold" CSI "m and " CSI "7mreverse" CSI "m inside.");
    /* 表 */
    static const char *const cell[3][3] = {
        { "Key", "Sends", "Mode" }, { "Up", "ESC [ A", "normal" }, { "Up", "ESC O A", "DECCKM" },
    };
    const int top = 4, left = 40, cw = 11;
    printf(ESC "(0");
    for (int r = 0; r <= 3; r++) {
        printf(CSI "%d;%dH", top + r * 2, left);
        vt_putc(r == 0 ? 'l' : r == 3 ? 'm' : 't');
        for (int c = 0; c < 3; c++) {
            for (int i = 0; i < cw; i++)
                vt_putc('q');
            vt_putc(c == 2 ? (r == 0 ? 'k' : r == 3 ? 'j' : 'u') : (r == 0 ? 'w' : r == 3 ? 'v' : 'n'));
        }
        if (r == 3)
            break;
        printf(CSI "%d;%dH", top + r * 2 + 1, left);
        for (int c = 0; c < 3; c++)
            printf("x" ESC "(B %-*s" ESC "(0", cw - 1, cell[r][c]);
        vt_putc('x');
    }
    printf(ESC "(B");
    printf(CSI "14;4HChecker " ESC "(0aaaaaaaaaa" ESC "(B  Scan lines " ESC "(0opqrs" ESC "(B  Symbols " ESC "(0`fgyz{|}~" ESC "(B");
    printf(CSI "15;4HControl pictures " ESC "(0bcdehi" ESC "(B");
    wait_key();
}

static void page_double(void)
{
    title("Double width and double height lines (DECDWL, DECDHL)");
    printf(ESC "#6Double width line\n");
    printf(ESC "#6" CSI "1mBold" CSI "m " CSI "4munderline" CSI "m " CSI "7mreverse" CSI "m\n\n");
    printf(ESC "#3Double height " CSI "7mVTerm" CSI "m\n");
    printf(ESC "#4Double height " CSI "7mVTerm" CSI "m\n\n");
    /* 倍高の行は、上半分 (ESC # 3) と下半分 (ESC # 4) の 2 行に同じ文字を書く */
    printf(ESC "#3" CSI "4mUnderlined" CSI "m and " ESC "(0lqwqk" ESC "(B\n");
    printf(ESC "#4" CSI "4mUnderlined" CSI "m and " ESC "(0lqwqk" ESC "(B\n\n");
    printf(ESC "#5Single width line (ESC # 5) for comparison.\n");
    printf(ESC "#6" CSI "5mBlinking double width" CSI "m\n\n");
    printf("Cursor moves on a double width line stop at column %d.\n", vt_cols() / 2);
    printf(ESC "#6" CSI "200C*");
    wait_key();
}

static void numbered_lines(int from, int n, const char *what)
{
    for (int i = 0; i < n; i++)
        printf("\n%s line %d", what, from + i);
}

static void page_scroll(void)
{
    const int rows = vt_rows();
    title("Scrolling region (DECSTBM), jump and smooth scroll (DECSCLM)");
    printf("Rows 5-%d are the scrolling region. The lines above and below stay.\n", rows - 4);
    printf(CSI "%d;1H" CSI "7m This line is below the region " CSI "m", rows - 2);
    printf(CSI "5;%dr", rows - 4);              /* 範囲を決めると、カーソルは左上へ */
    printf(CSI "%d;1H", rows - 4);
    numbered_lines(1, 30, "Jump scroll");
    pause_frames(60);
    printf(CSI "?4h");                          /* スムーズスクロール */
    numbered_lines(1, 12, "Smooth scroll");
    pause_frames(30);
    printf(CSI "5;1H");
    for (int i = 0; i < 6; i++) {               /* 範囲の上端で RI: 下へ流れる */
        printf(ESC "M" CSI "1mReverse index %d" CSI "m", i + 1);
        printf("\r");
    }
    printf(CSI "?4l");
    pause_frames(30);
    for (int i = 0; i < 5; i++) {               /* SU / SD */
        printf(CSI "2S");
        pause_frames(6);
    }
    for (int i = 0; i < 5; i++) {
        printf(CSI "2T");
        pause_frames(6);
    }
    printf(CSI "r");
    wait_key();
}

static void page_edit(void)
{
    title("Insert and delete (ICH, DCH, IL, DL, ECH, IRM)");
    for (int r = 0; r < 10; r++)
        printf("Line %d: ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz\n", r);
    pause_frames(30);
    printf(CSI "3;9H");
    for (int i = 0; i < 5; i++) {
        printf(CSI "2@");                       /* 2 文字挿入 */
        pause_frames(4);
    }
    printf(CSI "4;9H");
    for (int i = 0; i < 5; i++) {
        printf(CSI "2P");                       /* 2 文字削除 */
        pause_frames(4);
    }
    printf(CSI "5;9H" CSI "10X");               /* 10 文字消す (ずらさない) */
    printf(CSI "6;9H" CSI "4h" CSI "1m<inserted>" CSI "m" CSI "4l");
    pause_frames(30);
    printf(CSI "8;1H");
    for (int i = 0; i < 3; i++) {
        printf(CSI "L" CSI "7mInserted line %d" CSI "m", i + 1);  /* 行の挿入 */
        pause_frames(15);
    }
    for (int i = 0; i < 2; i++) {
        printf(CSI "M");                        /* 行の削除 */
        pause_frames(15);
    }
    printf(CSI "18;1HEL 0/1/2 on the next lines:\n");
    for (int m = 0; m < 3; m++)
        printf("0123456789012345678901234567890123456789" CSI "20D" CSI "%dK\n", m);
    wait_key();
}

static void page_bell(void)
{
    title("Bell (BEL, Ctrl-G)");
    printf("Ringing the bell three times...\n");
    for (int i = 0; i < 3; i++) {
        printf("\a*");
        pause_frames(30);
    }
    printf("\n");
    wait_key();
}

static void show_byte(int c)
{
    if (c == 0x1B)
        printf(CSI "7mESC" CSI "m ");
    else if (c < 0x20)
        printf("^%c ", c + '@');
    else if (c == 0x7F)
        printf("DEL ");
    else
        printf("%c ", c);
}

static void page_keys(void)
{
    title("Keyboard: bytes sent by each key (Ctrl-C to return)");
    printf("1: cursor keys normal/application (DECCKM)  2: keypad numeric/application (DECKPAM)\n");
    printf("3: ask the terminal for DA, CPR and size\n\n");
    int ckm = 0, kpam = 0;
    for (;;) {
        const int c = vt_getc_wait();
        if (c == 3)
            break;
        if (c == '1') {
            ckm ^= 1;
            printf(CSI "?1%c" "[cursor keys %s]\n", ckm ? 'h' : 'l', ckm ? "application" : "normal");
            continue;
        }
        if (c == '2') {
            kpam ^= 1;
            printf("%s[keypad %s]\n", kpam ? ESC "=" : ESC ">", kpam ? "application" : "numeric");
            continue;
        }
        if (c == '3') {
            printf(CSI "c" CSI "6n" CSI "18t");     /* 返事は入力の流れに入る */
            printf("[replies] ");
            continue;
        }
        printf("%02x=", c);
        show_byte(c);
        pause_frames(1);
        int d;
        while ((d = vt_getc()) >= 0) {          /* 同じキーの続き */
            printf("%02x=", d);
            show_byte(d);
        }
        printf("\n");
    }
    printf(CSI "?1l" ESC ">");
}

static void page_type(void)
{
    title("Loopback: what you type goes to the terminal (Ctrl-C to return)");
    printf("Try escape sequences, e.g.  Esc [ 7 m   Esc # 6   Esc ( 0   Esc [ 2 J\n");
    printf("Return = CR LF, Delete = BS space BS.\n\n");
    for (;;) {
        const int c = vt_getc_wait();
        if (c == 3)
            break;
        if (c == '\r')
            vt_puts("\r\n");
        else if (c == 0x7F)
            vt_puts("\b \b");
        else
            vt_putc(c);
    }
}

static unsigned to_us(uint32_t ticks)
{
    return (unsigned)((uint64_t)ticks * 1000000u / TIMER_HZ);
}

static void page_speed(void)
{
    title("Speed: plain text output with jump scroll");
    static const char line[] = "The quick brown fox jumps over the lazy dog. 0123456789 ABCDEFGHIJKLMNOPQRSTUVW\r\n";
    const int n = 300, len = sizeof line - 1;
    /* 1. 端末の中身だけ (画面に描かない) */
    uint32_t t0 = timer_now();
    for (int i = 0; i < n; i++)
        for (int k = 0; k < len; k++)
            vtcore_putc(line[k]);
    const uint32_t t_core = timer_now() - t0;
    /* 2. 画面全体を組み立てる / 写す */
    uint32_t t_render, t_write;
    vtdraw_measure(&t_render, &t_write);
    /* 3. printf から画面まで (ふだんの使い方) */
    t0 = timer_now();
    for (int i = 0; i < n; i++)
        printf("%3d %s", i, line + 4);
    vt_flush();
    const uint32_t t_all = timer_now() - t0;
    const unsigned ms = to_us(t_all) / 1000;
    printf(CSI "7m %d lines in %u ms = %u lines/s, %u characters/s " CSI "m\n",
           n, ms, n * 1000u / (ms ? ms : 1), n * (unsigned)len * 1000u / (ms ? ms : 1));
    printf(CSI "7m core only %u us/char, render 1 row %u us, write screen %u us " CSI "m\n",
           to_us(t_core) / (n * (unsigned)len), to_us(t_render) / (unsigned)vt_rows(), to_us(t_write));
    wait_key();
}

static void page_screen(void)
{
    title("Screen alignment (DECALN), reverse screen (DECSCNM), cursor shapes");
    pause_frames(30);
    printf(ESC "#8");                           /* 画面を E で埋める */
    pause_frames(60);
    printf(CSI "?5h");                          /* 画面全体を反転 */
    pause_frames(60);
    printf(CSI "?5l" CSI "2J" CSI "H");
    static const char *const shapes[] = {
        "blinking block", "steady block", "blinking underline", "steady underline",
        "blinking bar", "steady bar",
    };
    printf("Cursor shapes (DECSCUSR). Press a key for the next shape.\n\n");
    for (int i = 0; i < 6; i++) {
        printf(CSI "%d q" "%d %-20s", i + 1, i + 1, shapes[i]);
        get_key();
        printf("\n");
    }
    printf(CSI "0 q");
    printf("\nAlternate screen (?1049): press a key to switch.");
    get_key();
    printf(CSI "?1049h" CSI "1;1H" CSI "7m This is the alternate screen. " CSI "m\n\nPress a key to go back.");
    get_key();
    printf(CSI "?1049l");
    printf("\nBack on the main screen, cursor restored.");
    wait_key();
}

/* ---- メニュー ---- */

static void menu(void)
{
    printf(ESC "c");
    printf(ESC "#3      " CSI "1mVTermLib" CSI "m\n");
    printf(ESC "#4      " CSI "1mVTermLib" CSI "m\n");
    printf(ESC "#6  VT102 for the SE/30\n\n");
    static const char *const items[] = {
        "1  Character attributes and character sets",
        "2  Line drawing",
        "3  Double width / double height lines",
        "4  Scrolling region, jump and smooth scroll",
        "5  Insert and delete",
        "6  Bell",
        "7  Keyboard (bytes sent by keys, replies)",
        "8  Loopback typing",
        "9  Speed",
        "0  Alignment, reverse screen, cursor, alternate screen",
    };
    const int n = sizeof items / sizeof items[0];
    box(6, 6, n + 4, 60);
    for (int i = 0; i < n; i++)
        printf(CSI "%d;9H%s", 8 + i, items[i]);
    printf(CSI "%d;6H%d x %d characters, font %d x %d dots. RAM %u KB.",
           n + 11, vt_cols(), vt_rows(), VIDEO_W / vt_cols(), VIDEO_H / vt_rows(),
           (unsigned)(platform_ram_size() >> 10));
    printf(CSI "%d;6HChoose 0-9: ", n + 13);
}

int main(void)
{
    platform_init();
    keyboard_init();
    beep_init();
    vt_init();

    for (;;) {
        menu();
        switch (get_key()) {
        case '1': page_attrs(); break;
        case '2': page_lines(); break;
        case '3': page_double(); break;
        case '4': page_scroll(); break;
        case '5': page_edit(); break;
        case '6': page_bell(); break;
        case '7': page_keys(); break;
        case '8': page_type(); break;
        case '9': page_speed(); break;
        case '0': page_screen(); break;
        }
    }
}
