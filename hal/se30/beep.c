/**
 * @file beep.c
 * @brief ASC で短い音を鳴らす (beep.h)
 *
 * ASC (asc.c) を FIFO モードで使い、垂直同期の割り込み (platform.c) から asc_poll で補充する。
 * 音は正弦波を表から引き、終わり近くで音量を下げる (ぶつっと切れる音を避ける)。
 * サンプルは符号なし 8 ビット ($80 が無音)、約 22,254Hz。
 */
#include "beep.h"
#include "asc.h"

#define RATE     (22254u)               /* 1 秒のサンプル数 */
#define VOLUME   (5)                    /* スピーカーへの音量 (0-7) */
#define FADE     (RATE / 100)           /* 終わりの 10ms で音量を下げる */

static int8_t sine[256];                /* 正弦波 1 周期 (振幅 ±100) */
static volatile uint32_t left;          /* 残りのサンプル数 */
static volatile uint32_t step;          /* 1 サンプルで進む位相 (1 周期 = 2^32) */
static uint32_t phase;
static int ready;                       /* beep_init が済んだ */

/* 割り込みの中で呼ばれる: n 個作る */
static void render(volatile uint8_t *fa, volatile uint8_t *fb, uint32_t n)
{
    while (n--) {
        uint8_t v = 0x80;
        if (left) {
            int s = sine[phase >> 24];
            if (left < FADE)
                s = s * (int)left / (int)FADE;
            v = (uint8_t)(0x80 + s);
            phase += step;
            left--;
        }
        *fa = v;
        *fb = v;
    }
}

void beep_init(void)
{
    /* 正弦波の表 (浮動小数点を使わずに作る: 回転を少しずつ進める) */
    int32_t c = 1 << 14, s = 0;         /* cos, sin (2^14 = 1.0) */
    for (int i = 0; i < 256; i++) {
        sine[i] = (int8_t)(s * 100 >> 14);
        /* 2π/256 回す (cos, sin を少しずつ足す方法。誤差は 1 周で十分小さい) */
        const int32_t k = 402;          /* 2π/256 * 2^14 */
        c -= (s * k) >> 14;
        s += (c * k) >> 14;
    }
    asc_init(render, VOLUME);
    ready = 1;
}

void beep_play(int hz, int ms)
{
    if (hz <= 0 || ms <= 0)
        return;
    left = 0;                           /* 割り込みの中で途中の値を使わないよう、先に止める */
    step = (uint32_t)(((uint64_t)hz << 32) / RATE);
    phase = 0;
    left = (uint32_t)ms * RATE / 1000;
}

/* 垂直同期の割り込みから (platform.c) */
void beep_vsync(void)
{
    if (ready)
        asc_poll();
}
