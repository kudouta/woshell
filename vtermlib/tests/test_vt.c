/**
 * @file test_vt.c
 * @brief 端末の中身 (vtcore.c) を PC で確認する。make test
 *
 * 描く側 (vtdraw.c) の代わりに何もしない関数をつなぎ、シーケンスを流して文字の表と返事を調べる。
 */
#include <stdio.h>
#include <string.h>
#include "vtcore.h"
#include "keyboard.h"

/* ---- 描く側の代わり ---- */
static int bells, scrolls;
void vtdraw_dirty_all(void) {}
void vtdraw_scroll(int t, int b, int n) { (void)t; (void)b; (void)n; scrolls++; }
void vt_bell(void) { bells++; }

static int fails, checks;

static void feed(const char *s)
{
    while (*s)
        vtcore_putc((unsigned char)*s++);
}

/* 行 r の文字 (線画文字は '#'、右の空白は落とす) */
static const char *row(int r)
{
    static char buf[VTCORE_COLS + 1];
    const struct vtcore_cell *l = vtcore_line(r);
    for (int x = 0; x < VTCORE_COLS; x++)
        buf[x] = l[x].ch >= 0x80 ? '#' : (char)l[x].ch;
    int n = VTCORE_COLS;
    while (n > 0 && buf[n - 1] == ' ')
        n--;
    buf[n] = 0;
    return buf;
}

/* 入力の流れ (返事) を全部取り出す */
static const char *replies(void)
{
    static char buf[256];
    int n = 0, c;
    while ((c = vtcore_getc()) >= 0 && n < 255)
        buf[n++] = (char)c;
    buf[n] = 0;
    return buf;
}

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("%s:%d: NG: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; const char *a_ = (a), *b_ = (b); if (strcmp(a_, b_)) { fails++; \
    printf("%s:%d: NG: %s\n    got  \"%s\"\n    want \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static void fresh(void)
{
    vtcore_init();
    replies();
}

int main(void)
{
    char want[VTCORE_COLS + 2];

    /* 文字と折り返し */
    fresh();
    feed("hello");
    CHECK_STR(row(0), "hello");
    CHECK(vtcore_state.x == 5 && vtcore_state.y == 0);
    fresh();
    for (int i = 0; i < VTCORE_COLS; i++)
        feed("a");
    CHECK(vtcore_state.x == VTCORE_COLS - 1 && vtcore_state.y == 0);       /* 右端に書いた直後は、まだ右端にいる */
    feed("b");
    CHECK(vtcore_state.y == 1 && vtcore_state.x == 1);
    CHECK_STR(row(1), "b");
    /* 折り返しなし (DECAWM off) */
    fresh();
    feed("\033[?7l");
    for (int i = 0; i < VTCORE_COLS + 3; i++)
        feed("x");
    feed("Y");
    CHECK(vtcore_state.y == 0);
    CHECK(vtcore_line(0)[VTCORE_COLS - 1].ch == 'Y');

    /* カーソル移動 */
    fresh();
    feed("\033[5;10HX");
    CHECK(vtcore_line(4)[9].ch == 'X');
    feed("\033[2A\033[3DZ");
    CHECK(vtcore_line(2)[7].ch == 'Z');
    feed("\033[999;999H");
    CHECK(vtcore_state.x == VTCORE_COLS - 1 && vtcore_state.y == VTCORE_ROWS - 1);
    feed("\033[H\033[3d\033[7G*");
    CHECK(vtcore_line(2)[6].ch == '*');

    /* 消去 */
    fresh();
    feed("0123456789\r\n0123456789\r\n0123456789");
    feed("\033[2;5H\033[K");
    CHECK_STR(row(1), "0123");
    feed("\033[1;5H\033[1K");
    CHECK_STR(row(0), "     56789");
    feed("\033[2;3H\033[J");
    CHECK_STR(row(1), "01");
    CHECK_STR(row(2), "");
    feed("\033[2J");
    CHECK_STR(row(0), "");

    /* 挿入と削除 */
    fresh();
    feed("ABCDEFG\033[1;3H\033[2@");
    CHECK_STR(row(0), "AB  CDEFG");
    feed("\033[3P");
    CHECK_STR(row(0), "ABDEFG");
    feed("\033[2X");
    CHECK_STR(row(0), "AB  FG");
    feed("\033[4h**\033[4l");
    CHECK_STR(row(0), "AB**  FG");
    fresh();
    feed("L0\r\nL1\r\nL2\r\nL3\033[2;5H\033[L");
    CHECK_STR(row(1), "");
    CHECK_STR(row(2), "L1");
    CHECK(vtcore_state.x == 0);
    feed("\033[2M");
    CHECK_STR(row(1), "L2");
    CHECK_STR(row(2), "L3");

    /* スクロール範囲 */
    fresh();
    feed("top\033[2;4r");
    CHECK(vtcore_state.x == 0 && vtcore_state.y == 0);                  /* DECSTBM でカーソルは左上 */
    feed("\033[2;1HA\r\nB\r\nC\r\nD");
    CHECK_STR(row(0), "top");
    CHECK_STR(row(1), "B");
    CHECK_STR(row(2), "C");
    CHECK_STR(row(3), "D");
    feed("\033[2;1H\033M");                         /* 上端で RI: 下へ流れる */
    CHECK_STR(row(1), "");
    CHECK_STR(row(2), "B");
    CHECK_STR(row(3), "C");
    feed("\033[S");
    CHECK_STR(row(1), "B");
    feed("\033[r");

    /* 画面全体のスクロール */
    fresh();
    for (int i = 0; i <= VTCORE_ROWS; i++) {
        char s[16];
        snprintf(s, sizeof s, "%sline%d", i ? "\r\n" : "", i);
        feed(s);
    }
    CHECK_STR(row(0), "line1");
    snprintf(want, sizeof want, "line%d", VTCORE_ROWS);
    CHECK_STR(row(VTCORE_ROWS - 1), want);

    /* 返事: DA, DSR, CPR, DECOM */
    fresh();
    feed("\033[c");
    CHECK_STR(replies(), "\033[?6c");
    feed("\033[5n");
    CHECK_STR(replies(), "\033[0n");
    feed("\033[3;7H\033[6n");
    CHECK_STR(replies(), "\033[3;7R");
    feed("\033[5;10r\033[?6h\033[1;1H\033[6n");
    CHECK_STR(replies(), "\033[1;1R");
    CHECK(vtcore_state.y == 4);
    feed("\033[99;1H");
    CHECK(vtcore_state.y == 9);                               /* DECOM では範囲の外へ出ない */
    feed("\033[?6l\033[r\033[18t");
    snprintf(want, sizeof want, "\033[8;%d;%dt", VTCORE_ROWS, VTCORE_COLS);
    CHECK_STR(replies(), want);

    /* タブ */
    fresh();
    feed("\tX");
    CHECK(vtcore_line(0)[8].ch == 'X');
    feed("\033[3g\r\033[5C\033H\r\tY");
    CHECK(vtcore_line(0)[5].ch == 'Y');
    feed("\r\033[3g\tZ");
    CHECK(vtcore_line(0)[VTCORE_COLS - 1].ch == 'Z');

    /* 属性と文字の組 */
    fresh();
    feed("\033[1;4mA\033[7mB\033[0mC\033[5;8mD\033[mE");
    CHECK(vtcore_line(0)[0].attr == (VTCORE_ATTR_BOLD | VTCORE_ATTR_UNDERLINE));
    CHECK(vtcore_line(0)[1].attr == (VTCORE_ATTR_BOLD | VTCORE_ATTR_UNDERLINE | VTCORE_ATTR_REVERSE));
    CHECK(vtcore_line(0)[2].attr == 0);
    CHECK(vtcore_line(0)[3].attr == (VTCORE_ATTR_BLINK | VTCORE_ATTR_INVISIBLE));
    CHECK(vtcore_line(0)[4].attr == 0);
    feed("\033[38;2;1;2;3;1mF");                    /* 色の引数は読み飛ばす */
    CHECK(vtcore_line(0)[5].attr == VTCORE_ATTR_BOLD);
    fresh();
    feed("\033(0qx\033(Bq");
    CHECK(vtcore_line(0)[0].ch == FONT_DEC_BASE + 'q' - 0x5F);
    CHECK(vtcore_line(0)[1].ch == FONT_DEC_BASE + 'x' - 0x5F);
    CHECK(vtcore_line(0)[2].ch == 'q');
    feed("\033)0a\016a\017a");
    CHECK(vtcore_line(0)[3].ch == 'a');
    CHECK(vtcore_line(0)[4].ch == FONT_DEC_BASE + 'a' - 0x5F);
    CHECK(vtcore_line(0)[5].ch == 'a');

    /* 倍幅の行 */
    fresh();
    feed("\033#6\033[200C");
    CHECK(vtcore_state.x == VTCORE_COLS / 2 - 1);
    CHECK(vtcore_lattr(0) == VTCORE_LINE_DOUBLE_WIDTH);
    feed("\033#3");
    CHECK(vtcore_lattr(0) == VTCORE_LINE_DOUBLE_TOP);
    feed("\033[2J");
    CHECK(vtcore_lattr(0) == VTCORE_LINE_NORMAL);

    /* DECALN */
    fresh();
    feed("\033#8");
    for (int i = 0; i < VTCORE_COLS; i++)
        want[i] = 'E';
    want[VTCORE_COLS] = 0;
    CHECK_STR(row(0), want);
    CHECK_STR(row(VTCORE_ROWS - 1), want);

    /* 代わりの画面 */
    fresh();
    feed("main\033[?1049h");
    CHECK_STR(row(0), "");
    feed("alt");
    CHECK_STR(row(0), "    alt");                   /* xterm と同じく、切り替えてもカーソルは動かない */
    feed("\033[?1049l");
    CHECK_STR(row(0), "main");
    CHECK(vtcore_state.x == 4 && vtcore_state.y == 0);

    /* カーソルを覚える / 戻す */
    fresh();
    feed("\033[3;4H\033[1m\0337\033[H\033[m\0338X");
    CHECK(vtcore_line(2)[3].ch == 'X' && vtcore_line(2)[3].attr == VTCORE_ATTR_BOLD);

    /* 文字列は読み捨てる, CAN で中止 */
    fresh();
    feed("\033]0;window title\007A\033P1$r\033\\B\033[1\030C");
    CHECK_STR(row(0), "ABC");

    /* REP, ベル */
    fresh();
    feed("a\033[3b\007");
    CHECK_STR(row(0), "aaaa");
    CHECK(bells == 1);

    /* キー */
    fresh();
    vtcore_key('x');
    vtcore_key(KEYBOARD_UP);
    CHECK_STR(replies(), "x\033[A");
    feed("\033[?1h");
    vtcore_key(KEYBOARD_UP);
    vtcore_key(KEYBOARD_F1);
    vtcore_key(KEYBOARD_F5);
    vtcore_key(KEYBOARD_PGDN);
    CHECK_STR(replies(), "\033OA\033OP\033[15~\033[6~");
    vtcore_key(KEYBOARD_KP | '5');
    feed("\033=");
    vtcore_key(KEYBOARD_KP | '5');
    vtcore_key(KEYBOARD_KP | '\r');
    vtcore_key(KEYBOARD_KP | '*');
    CHECK_STR(replies(), "5\033Ou\033OM\033OS");
    feed("\033[20h");
    vtcore_key('\r');
    CHECK_STR(replies(), "\r\n");

    /* RIS */
    feed("\033[5;10r\033[?6h\033c\033[24;1H\033[6n");
    CHECK_STR(replies(), "\033[24;1R");

    printf("%d checks, %d failed (%d x %d characters)\n", checks, fails, VTCORE_COLS, VTCORE_ROWS);
    return fails != 0;
}
