/**
 * @file via.h
 * @brief SE/30 の VIA (6522) と時計 (SE/30 の中だけで使う)
 *
 * VIA1 の番地はローメモリ変数 VIA ($01D4) から取る。VIA2 は VIA1 の $2000 先
 * (MAME の macii.cpp: VIA1 $50F00000, VIA2 $50F02000)。レジスタは $200 おき。
 * VIA のクロックは C7M/10 = 783.36kHz (1 カウント 約 1.277us)。
 */
#ifndef SE30_VIA_H
#define SE30_VIA_H

#include <stdint.h>

/** VIA のレジスタ (VIA の先頭からの位置) */
enum {
    VIA_ORB  = 0x0000, VIA_ORA_H = 0x0200, VIA_DDRB = 0x0400, VIA_DDRA = 0x0600,
    VIA_T1CL = 0x0800, VIA_T1CH  = 0x0A00, VIA_T1LL = 0x0C00, VIA_T1LH = 0x0E00,
    VIA_T2CL = 0x1000, VIA_T2CH  = 0x1200, VIA_SR   = 0x1400, VIA_ACR  = 0x1600,
    VIA_PCR  = 0x1800, VIA_IFR   = 0x1A00, VIA_IER  = 0x1C00, VIA_ORA  = 0x1E00,
};

/* IFR / IER のビット */
#define VIA_IRQ_CA2  (0x01)
#define VIA_IRQ_CA1  (0x02)
#define VIA_IRQ_SR   (0x04)
#define VIA_IRQ_CB2  (0x08)
#define VIA_IRQ_CB1  (0x10)
#define VIA_IRQ_T2   (0x20)
#define VIA_IRQ_T1   (0x40)
#define VIA_IRQ_SET  (0x80)

/** VIA のクロック (Hz) */
#define VIA_HZ (783360u)

/** VIA1 の先頭 (via_init で決まる) */
extern volatile uint8_t *via_1;
/** VIA2 の先頭 (via_init で決まる) */
extern volatile uint8_t *via_2;

/**
 * @brief VIA のレジスタを読む
 * @param via VIA の先頭 (via_1 か via_2)
 * @param reg レジスタ (VIA_ORB など)
 * @return 値
 */
static inline uint8_t via_r(volatile uint8_t *via, uint32_t reg)
{
    return via[reg];
}

/**
 * @brief VIA のレジスタに書く
 * @param via VIA の先頭 (via_1 か via_2)
 * @param reg レジスタ (VIA_ORB など)
 * @param v   値
 */
static inline void via_w(volatile uint8_t *via, uint32_t reg, uint8_t v)
{
    via[reg] = v;
}

/**
 * @brief VIA の番地を決め、ROM が有効にした割り込みを止め、時計 (タイマ2) を回し始める
 *
 * platform_init の最初に呼ぶ。
 */
void via_init(void);

/**
 * @brief VIA1 タイマ2 を 16 ビットの減算カウンタとして読む (約 83.7ms で一周)
 * @return 今の値 (減っていく)
 */
uint16_t via_timer(void);

/**
 * @brief 32 ビットの時計 (VIA のカウント, 増えていく)。約 91 分で一周する
 *
 * 一周 (83.7ms) に 1 回以上読む必要があるので、垂直同期の割り込みでも読んでいる。
 * @return 今の値
 */
uint32_t via_clock(void);

/**
 * @brief start から今までのカウント数 (一周未満であること)
 * @param start 前に via_timer で読んだ値
 * @return 経ったカウント数
 */
static inline uint16_t via_timer_since(uint16_t start)
{
    return (uint16_t)(start - via_timer());
}

#endif
