/**
 * @file fmt.c
 * @brief printf / sprintf (機種に依存しない。printf の出力先は、使う側が用意する fmt_putc)
 *
 * 使える書式: %d %i %u %x %X %o %c %s %p %%。フラグ '-' '0'、幅 (数字か '*')、精度 ('.' と数字。%s のみ)。
 * 'l' 'h' は読み飛ばす (int と long はどちらも 32 ビット)。
 */
#include <stdarg.h>
#include <stdint.h>
#include "fmt.h"

struct out {
    void (*put)(struct out *o, int c);
    char *buf;                          /* sprintf の書き込み先 */
    int n;                              /* 出した文字数 */
};

static void put_console(struct out *o, int c)
{
    fmt_putc(c);
    o->n++;
}

static void put_buf(struct out *o, int c)
{
    o->buf[o->n++] = (char)c;
}

static void pad(struct out *o, int n, int c)
{
    while (n-- > 0)
        o->put(o, c);
}

static void format(struct out *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            o->put(o, *fmt);
            continue;
        }
        int left = 0, zero = 0, width = 0, prec = -1;
        for (;; fmt++) {
            if (fmt[1] == '-')
                left = 1;
            else if (fmt[1] == '0')
                zero = 1;
            else
                break;
        }
        fmt++;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'h')
            fmt++;
        if (!*fmt)
            break;

        char tmp[12];
        const char *s = tmp;
        int len = 0, neg = 0;
        switch (*fmt) {
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (s[len] && (prec < 0 || len < prec))
                len++;
            break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            uint32_t v = va_arg(ap, uint32_t);
            unsigned base = 10;
            const char *digits = "0123456789abcdef";
            if ((*fmt == 'd' || *fmt == 'i') && (int32_t)v < 0) {
                neg = 1;
                v = -v;
            }
            if (*fmt == 'x' || *fmt == 'p')
                base = 16;
            else if (*fmt == 'X') {
                base = 16;
                digits = "0123456789ABCDEF";
            } else if (*fmt == 'o')
                base = 8;
            char *p = tmp + sizeof tmp;
            do {
                *--p = digits[v % base];
                v /= base;
            } while (v);
            s = p;
            len = (int)(tmp + sizeof tmp - p);
            break;
        }
        case '%':
            o->put(o, '%');
            continue;
        default:                        /* 知らない書式はそのまま出す */
            o->put(o, '%');
            o->put(o, *fmt);
            continue;
        }
        const int total = len + neg;
        if (!left && !zero)
            pad(o, width - total, ' ');
        if (neg)
            o->put(o, '-');
        if (!left && zero)
            pad(o, width - total, '0');
        for (int i = 0; i < len; i++)
            o->put(o, s[i]);
        if (left)
            pad(o, width - total, ' ');
    }
}

int vprintf(const char *fmt, va_list ap)
{
    struct out o = { put_console, 0, 0 };
    format(&o, fmt, ap);
    return o.n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    struct out o = { put_buf, buf, 0 };
    format(&o, fmt, ap);
    buf[o.n] = 0;
    return o.n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
