/**
 * @file via.c
 * @brief SE/30 の VIA (6522) の初期化と時計 (via.h, timer.h)
 *
 * 時計: VIA1 タイマ2 (16 ビット, 減っていく, 約 83.7ms で一周) を読むたびに、前回からの差を足し込んで
 * 32 ビットにする。垂直同期の割り込みの中でも読むので、一周する前に必ず更新される。
 */
#include "via.h"
#include "irq.h"
#include "timer.h"
#include "mac.h"

volatile uint8_t *via_1, *via_2;

static uint32_t clock_base;
static uint16_t clock_last;

uint16_t via_timer(void)
{
    uint8_t hi, lo;
    do {
        hi = via_r(via_1, VIA_T2CH);
        lo = via_r(via_1, VIA_T2CL);
    } while (hi != via_r(via_1, VIA_T2CH));   /* 下位の桁上がりと重なったら読み直す */
    return (uint16_t)(hi << 8 | lo);
}

uint32_t via_clock(void)
{
    uint16_t sr = irq_disable();
    uint16_t t = via_timer();
    clock_base += (uint16_t)(clock_last - t);
    clock_last = t;
    uint32_t v = clock_base;
    irq_restore(sr);
    return v;
}

uint32_t timer_now(void)
{
    return via_clock();
}

void via_init(void)
{
    via_1 = (volatile uint8_t *)MAC_LOWMEM_L(0x01D4);  /* ローメモリ変数 VIA */
    via_2 = via_1 + 0x2000;

    /* ROM が有効にした割り込みをすべて止め、残っているフラグも消す */
    via_w(via_1, VIA_IER, 0x7F);
    via_w(via_2, VIA_IER, 0x7F);
    via_w(via_1, VIA_IFR, 0x7F);
    via_w(via_2, VIA_IFR, 0x7F);

    /* VIA1 タイマ2 を単発モードで回し続ける (0 を過ぎても減り続けるので時計に使える) */
    via_w(via_1, VIA_ACR, via_r(via_1, VIA_ACR) & ~0x20);
    via_w(via_1, VIA_T2CL, 0xFF);
    via_w(via_1, VIA_T2CH, 0xFF);
    clock_last = via_timer();
}
