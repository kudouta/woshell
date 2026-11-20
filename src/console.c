/**
 * @file console.c
 * @brief 文字の画面 (console.h)。VTermLib の端末に渡し、シリアルにも同じものを送る
 */
#include "console.h"
#include "vt.h"
#include "serial.h"
#include "fmt.h"

static int serial_out = 1;              /* シリアルにも出す (console_serial) */

void console_init(void)
{
    vt_init();
}

void console_serial(int on)
{
    serial_out = on;
}

void console_putc(int c)
{
    if (serial_out) {
        if (c == '\n')
            serial_putc('\r');
        serial_putc(c);
    }
    vt_putc(c);                         /* vt は '\n' を "\r\n" にする (onlcr) */
}

void console_puts(const char *s)
{
    while (*s)
        console_putc((unsigned char)*s++);
}

void console_redraw(void)
{
    vt_redraw();
}

void console_clear(void)
{
    console_puts("\033[H\033[2J");      /* 画面とシリアルの端末の両方を消す */
}

int console_col(void)
{
    return vt_cursor_col();
}

int console_cols(void)
{
    return vt_cols();
}

int console_rows(void)
{
    return vt_rows();
}

void console_cursor(int show)
{
    vt_puts(show ? "\033[?25h" : "\033[?25l");  /* 画面だけ (シリアルの端末のカーソルはそのまま) */
}

void console_flush(void)
{
    vt_flush();
}

/* printf の出力先 (fmt.h) */
void fmt_putc(int c)
{
    console_putc(c);
}
