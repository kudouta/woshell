/**
 * @file vt.c
 * @brief 端末のインターフェース (vt.h)。中身 (vtcore.c)、描く側 (vtdraw.c)、キーボードをつなぐ
 */
#include "vt.h"
#include "vtcore.h"
#include "video.h"
#include "keyboard.h"
#include "beep.h"

#ifndef AUTO_FLUSH_VSYNCS
#define AUTO_FLUSH_VSYNCS (3)           /* 出力が続くときも、垂直同期 3 回 (約 1/20 秒) ごとに画面に出す */
#endif

static int onlcr = 1;
static uint32_t last_flush_vs;

void vt_init(void)
{
    vtdraw_init();
    vtcore_init();
    vt_flush();
}

void vt_flush(void)
{
    last_flush_vs = video_vsync_count();
    vtdraw_flush();
}

void vt_putc(int c)
{
    if (c == '\n' && onlcr)
        vtcore_putc('\r');
    vtcore_putc(c);
    /* 時計 (VIA) を読むのは遅いので、1 文字ごとには読まず、垂直同期の回数で見る */
    if (video_vsync_count() - last_flush_vs >= AUTO_FLUSH_VSYNCS)
        vt_flush();
}

void vt_write(const char *s, int n)
{
    while (n-- > 0)
        vt_putc((unsigned char)*s++);
}

void vt_puts(const char *s)
{
    while (*s)
        vt_putc((unsigned char)*s++);
}

void vt_set_onlcr(int on)
{
    onlcr = on;
}

void vt_idle(void)
{
    for (int k; (k = keyboard_getc()) >= 0; )
        vtcore_key(k);
    vt_flush();
}

int vt_getc(void)
{
    int c = vtcore_getc();
    if (c >= 0)
        return c;
    vt_idle();
    return vtcore_getc();
}

int vt_getc_wait(void)
{
    for (;;) {
        const int c = vt_getc();
        if (c >= 0)
            return c;
    }
}

int vt_cols(void)
{
    return VTCORE_COLS;
}

int vt_rows(void)
{
    return VTCORE_ROWS;
}

int vt_cursor_col(void)
{
    return vtcore_state.x;
}

void vt_redraw(void)
{
    vtdraw_dirty_all();
    vt_flush();
}

void vt_draw_bitmap(int x, int y, int w, int h, const uint32_t *bits)
{
    vt_flush();                         /* 今の文字を描き終えてから重ねる (後で描き直されないように) */
    vtdraw_bitmap(x, y, w, h, bits);
    vt_flush();
}

/* ベル (BEL, Ctrl-G) */
void vt_bell(void)
{
    beep_play(880, 120);
}
