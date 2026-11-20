/**
 * @file video.c
 * @brief SE/30 の内蔵ディスプレイ (512 x 342, 1bpp, 1 = 黒, 1 行 64 バイト)
 *
 * 画面の並びが video.h の形 (32 ビットの語, 最上位ビットが左端) と同じなので、そのまま写す。
 */
#include "video.h"
#include "mac.h"

extern volatile uint32_t irq_vsync_count;   /* platform.c */
extern volatile uint32_t irq_vsync_time;    /* platform.c */

void video_write(int y, int w0, int w1, const uint32_t *row)
{
    volatile uint32_t *p = (volatile uint32_t *)(ScrnBase + (uint32_t)y * ScreenRow);
    for (int w = w0; w <= w1; w++)
        p[w] = row[w];
}

/* 1 行 (16 語) を写す: move.l (a0)+,(a1)+ を 16 個並べる */
static inline void move_row(volatile uint32_t *d, const volatile uint32_t *s)
{
    __asm__ volatile(
        "move.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\t"
        "move.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\t"
        "move.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\t"
        "move.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+\n\tmove.l (%1)+, (%0)+"
        : "+a"(d), "+a"(s) : : "memory");
}

void video_copy(int wx, int y, int nw, int rows, const uint32_t *src)
{
    volatile uint32_t *p = (volatile uint32_t *)(ScrnBase + (uint32_t)y * ScreenRow) + wx;
    const uint32_t skip = ScreenRow / 4 - (uint32_t)nw;     /* 行の終わりから次の行の始まりまで (語) */
    if (nw == 16 && wx == 0 && ScreenRow == 64) {    /* 画面の幅いっぱい: 1 行を move.l 16 個で写す */
        for (int r = 0; r < rows; r++, p += 16, src += 16)
            move_row(p, src);
        return;
    }
    if (nw == 8) {                      /* 256 ドット幅: 行ごとに 8 語を並べて書く */
        for (int r = 0; r < rows; r++, p += skip, src += 8) {
            p[0] = src[0]; p[1] = src[1]; p[2] = src[2]; p[3] = src[3];
            p[4] = src[4]; p[5] = src[5]; p[6] = src[6]; p[7] = src[7];
            p += 8;
        }
        return;
    }
    for (int r = 0; r < rows; r++, p += skip)
        for (int w = 0; w < nw; w++)
            *p++ = *src++;
}

void video_move_rows(int dst_y, int src_y, int rows)
{
    const uint32_t row_bytes = ScreenRow;
    volatile uint8_t *d = (volatile uint8_t *)(ScrnBase + (uint32_t)dst_y * row_bytes);
    const volatile uint8_t *s = (const volatile uint8_t *)(ScrnBase + (uint32_t)src_y * row_bytes);
    if (dst_y > src_y) {                /* 下へずらすときは下から写す (重なりを壊さない) */
        d += (uint32_t)(rows - 1) * row_bytes;
        s += (uint32_t)(rows - 1) * row_bytes;
        for (int r = 0; r < rows; r++, d -= row_bytes, s -= row_bytes)
            move_row((volatile uint32_t *)d, (const volatile uint32_t *)s);
        return;
    }
    for (int r = 0; r < rows; r++, d += row_bytes, s += row_bytes)
        move_row((volatile uint32_t *)d, (const volatile uint32_t *)s);
}

uint32_t video_read_rows(int y0, int y1)
{
    const uint32_t row_bytes = ScreenRow;
    uint32_t sum = 0;
    for (int y = y0; y < y1; y++) {
        const volatile uint32_t *p = (const volatile uint32_t *)(ScrnBase + (uint32_t)y * row_bytes);
        for (int w = 0; w < VIDEO_WORDS; w++)
            sum += p[w];
    }
    return sum;
}

void video_fill(int y0, int y1, uint32_t v)
{
    const uint32_t row_bytes = ScreenRow;
    for (int y = y0; y < y1; y++) {
        volatile uint32_t *p = (volatile uint32_t *)(ScrnBase + (uint32_t)y * row_bytes);
        for (int w = 0; w < VIDEO_WORDS; w++)
            p[w] = v;
    }
}

uint32_t video_vsync_count(void)
{
    return irq_vsync_count;
}

uint32_t video_vsync_time(void)
{
    return irq_vsync_time;
}

uint32_t video_wait_vsync(void)
{
    uint32_t n = irq_vsync_count;
    while (irq_vsync_count == n)
        ;
    return irq_vsync_count;
}
