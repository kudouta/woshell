/**
 * @file mem.c
 * @brief memset / memcpy
 *
 * libgcc にはなく、Cライブラリもリンクしていないので自前で用意する。
 * GCC は構造体のコピーや大きな配列の初期化などで、これらを自分から呼ぶことがある
 * (-ffreestanding でも memcpy / memmove / memset / memcmp はある前提でコードを作る)。
 *
 * 書き込み先を 4 バイト境界にそろえてから 4 バイトずつ (16 バイトずつ展開) 処理し、残りを 1 バイトずつ処理する。
 * 68030 は境界のずれた位置からの 4 バイトの読み書きができる (遅くなるだけ) ので、
 * memcpy の読み出し元のずれはそのままにする。
 *
 * -O2 以上では、GCC が「1 バイトずつ埋めるループ」を memset / memcpy の呼び出しに置き換えることがあり、
 * ここでそれが起きると自分自身を呼び続けてしまう。この 2 つの関数ではその最適化を切る。
 */
#include <stdint.h>

#ifndef MEM_PREFIX
#define MEM_PREFIX(name) name           /* PC での確認用に名前を変えられるようにする */
#endif

#define NO_LOOP_TO_CALL __attribute__((optimize("no-tree-loop-distribute-patterns")))

/* 境界のずれた位置も指せる 4 バイト (別の型のデータを読み書きしてよいことも示す) */
typedef uint32_t word_t __attribute__((aligned(1), may_alias));

NO_LOOP_TO_CALL void *MEM_PREFIX(memset)(void *dst, int c, unsigned long n)
{
    uint8_t *p = dst;
    const uint8_t b = (uint8_t)c;
    while (n && ((uintptr_t)p & 3)) {
        *p++ = b;
        n--;
    }
    const uint32_t w = b * 0x01010101u;
    word_t *q = (word_t *)p;
    for (; n >= 16; n -= 16, q += 4) {
        q[0] = w;
        q[1] = w;
        q[2] = w;
        q[3] = w;
    }
    for (; n >= 4; n -= 4)
        *q++ = w;
    p = (uint8_t *)q;
    while (n--)
        *p++ = b;
    return dst;
}

NO_LOOP_TO_CALL void *MEM_PREFIX(memcpy)(void *dst, const void *src, unsigned long n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n && ((uintptr_t)d & 3)) {
        *d++ = *s++;
        n--;
    }
    word_t *dw = (word_t *)d;
    const word_t *sw = (const word_t *)s;
    for (; n >= 16; n -= 16, dw += 4, sw += 4) {
        dw[0] = sw[0];
        dw[1] = sw[1];
        dw[2] = sw[2];
        dw[3] = sw[3];
    }
    for (; n >= 4; n -= 4)
        *dw++ = *sw++;
    d = (uint8_t *)dw;
    s = (const uint8_t *)sw;
    while (n--)
        *d++ = *s++;
    return dst;
}
