/**
 * @file irq.c
 * @brief SE/30 の割り込みと、その記録 (irq.h, platform.h の platform_irq_*)
 *
 * 割り込み (68030 の自動ベクタ。SE/30 の GLU が各回路の割り込みをレベルにまとめる):
 *   スプリアス (ベクタ 24): 割り込みの受け付け中に要求が消えたとき。数えるだけ
 *   レベル1 (VIA1): 要因は IFR で見分ける
 *       CA2 = 時計の 1 秒, CA1 = 60.15Hz, SR = ADB のシフトレジスタ, CB1/CB2 = ADB のクロック/データ,
 *       T1 / T2 = タイマ。使うのは SR (ADB, adb.c) だけ。T2 は割り込みにせず時計に使う (via.c)
 *   レベル2 (VIA2): 要因は IFR で見分ける
 *       CA1 = スロット (内蔵ビデオの垂直同期と PDS のカード), CA2 = SCSI の DRQ, CB1 = ASC (音),
 *       CB2 = SCSI の IRQ, T1 / T2 = タイマ。使うのは CA1 (垂直同期) だけ。実機で 59.83Hz。
 *       垂直同期のたびに、ベルの音 (ASC の FIFO) を補充する (beep.c)。
 *       VIA1 PB6 を 1 にすると下げる。MAME は 2 フレームに 1 回 (約 30Hz) しか入らない
 *   レベル3: SE/30 ではつながっていない。数えるだけ
 *   レベル4 (SCC): シリアル。最初は SCC の割り込みを止めておく。入ったら serial_irq (serial.c) に渡し、
 *       処理されなければ要因 (RR3) を残してもう一度止める
 *   レベル5, 6: SE/30 ではつながっていない。数えるだけ
 *   レベル7 (ベクタ 31): 割り込みボタン (NMI)。例外と同じエントリー (trapent.S) に入れ、
 *       trap_catch で構えていればそこへ戻る (構えていなければ trap.c が無視する)。エントリーは trap_nmi_entry で、
 *       1 回の押下で続けて入る NMI (実機で 2 万回以上) をまとめ、押下ごとに 1 回だけ trap_exc_entry へ進む
 * 例外 (バスエラーなど) も trapent.S のエントリーに入れる。
 *
 * 処理する相手がない要因が入ったら、要因を消して数える。
 * レベル3/5/6 は要因を消す方法がないので、もし入り続けると戻れない (割り込みボタンでは抜けられる)。
 */
#include "irq.h"
#include "platform.h"
#include "trap.h"
#include "mac.h"
#include "via.h"
#include "adb.h"
#include "beep.h"

volatile uint32_t irq_vsync_count;
volatile uint32_t irq_vsync_time;

/* 割り込みの記録 (レベル 0 = スプリアス ... 7 = 割り込みボタン) */
static volatile uint32_t count[PLATFORM_IRQ_LEVELS];
static volatile uint32_t unhandled_count[PLATFORM_IRQ_LEVELS];
static volatile uint32_t unhandled_detail[PLATFORM_IRQ_LEVELS];

#define VIA1_PB_VSYNC_OFF (0x40)    /* VIA1 ポートB ビット6: 1 にすると垂直同期の割り込みを下げる */

/* SCC (Z85C30)。SE/30 では 1 バイトずつ、B の制御 +0, A の制御 +2, B のデータ +4, A のデータ +6 */
#define SCC_CTRL_A  (2)
#define SCC_WR9     (9)             /* 割り込みの全体の許可 (ビット3 = MIE) とリセット */
#define SCC_RR3     (3)             /* 割り込みの要因 (A でだけ読める) */
static volatile uint8_t *scc;

/* エントリー (vectors.S, trapent.S) */
extern void irq_spurious_entry(void);
extern void irq_level1_entry(void);
extern void irq_level2_entry(void);
extern void irq_level3_entry(void);
extern void irq_level4_entry(void);
extern void irq_level5_entry(void);
extern void irq_level6_entry(void);
extern void trap_exc_entry(void);
extern void trap_nmi_entry(void);
extern volatile uint32_t trap_nmi_total, trap_nmi_presses, trap_nmi_burst_n, trap_nmi_burst_ticks;

uint16_t irq_disable(void)
{
    uint16_t sr;
    __asm__ volatile("move.w %%sr, %0\n\tori.w #0x0700, %%sr" : "=d"(sr) :: "memory");
    return sr;
}

void irq_restore(uint16_t sr)
{
    __asm__ volatile("move.w %0, %%sr" :: "d"(sr) : "memory");
}

static uint32_t get_vbr(void)
{
    uint32_t vbr;
    __asm__ volatile("movec %%vbr, %0" : "=d"(vbr));
    return vbr;
}

static void vsync_ack(void)
{
    uint8_t pb = via_r(via_1, VIA_ORB);
    via_w(via_1, VIA_ORB, pb | VIA1_PB_VSYNC_OFF);
    via_w(via_1, VIA_ORB, pb & ~VIA1_PB_VSYNC_OFF);
}

/* ---- SCC ----
 * SCC は続けて読み書きすると間に合わないことがあるので (約 2.2us 空ける)、VIA を 2 回読んで待つ */
static void scc_wait(void)
{
    (void)via_r(via_1, VIA_ORB);
    (void)via_r(via_1, VIA_ORB);
}

static uint8_t scc_read_reg(int reg)
{
    scc[SCC_CTRL_A] = (uint8_t)reg;
    scc_wait();
    const uint8_t v = scc[SCC_CTRL_A];
    scc_wait();
    return v;
}

static void scc_write_reg(int reg, uint8_t v)
{
    scc[SCC_CTRL_A] = (uint8_t)reg;
    scc_wait();
    scc[SCC_CTRL_A] = v;
    scc_wait();
}

/* SCC の割り込みを止める (ほかの設定はそのまま) */
static void scc_irq_off(void)
{
    (void)scc[SCC_CTRL_A];          /* 読むとレジスタの指定が 0 に戻る (前の途中の状態を捨てる) */
    scc_wait();
    scc_write_reg(SCC_WR9, 0x00);   /* MIE = 0。リセットはしない */
}

void irq_init(void)
{
    scc = (volatile uint8_t *)SCCRd;

    /* 例外のベクタ (2-63) を差し替える。24-30 は割り込み (自動ベクタ)、31 はレベル7 = 割り込みボタン */
    volatile uint32_t *vec = (volatile uint32_t *)get_vbr();
    for (int v = 2; v < 64; v++)
        vec[v] = (uint32_t)trap_exc_entry;
    vec[24] = (uint32_t)irq_spurious_entry;
    vec[25] = (uint32_t)irq_level1_entry;
    vec[26] = (uint32_t)irq_level2_entry;
    vec[27] = (uint32_t)irq_level3_entry;
    vec[28] = (uint32_t)irq_level4_entry;
    vec[29] = (uint32_t)irq_level5_entry;
    vec[30] = (uint32_t)irq_level6_entry;
    vec[31] = (uint32_t)trap_nmi_entry;         /* レベル7: 押下ごとに 1 回だけ trap_exc_entry へ */

    /* SCC (レベル4) は、使う側が serial_irq を用意して設定するまで止めておく */
    scc_irq_off();
}

void irq_enable(void)
{
    /* 垂直同期の割り込み。ROM のときに出た割り込みが残っているので、許可する前に一度下げる
       (下げないと CA1 に「下がる」が来ず、割り込みが入らない。実機で確認した) */
    vsync_ack();
    via_w(via_2, VIA_IFR, VIA_IRQ_CA1);
    via_w(via_2, VIA_IER, VIA_IRQ_SET | VIA_IRQ_CA1);

    __asm__ volatile("move.w #0x2000, %%sr" ::: "memory");
}

/* 処理する相手がなかった割り込み。detail は要因のビット (VIA の IFR, SCC の RR3 など) */
static void unhandled(int level, uint32_t detail)
{
    unhandled_count[level]++;
    unhandled_detail[level] = detail;
}

void irq_spurious(void)
{
    count[0]++;
    unhandled(0, 0);
}

void irq_level1(void)
{
    count[1]++;
    const uint8_t ifr = via_r(via_1, VIA_IFR) & via_r(via_1, VIA_IER) & 0x7F;
    if (ifr & VIA_IRQ_SR)
        adb_irq();
    const uint8_t other = ifr & ~VIA_IRQ_SR;    /* CA1, CA2, CB1, CB2, T1, T2 は使っていない */
    if (other)
        via_w(via_1, VIA_IFR, other);
    if (other || !ifr)
        unhandled(1, other);
    (void)via_clock();              /* 時計を進めておく */
}

void irq_level2(void)
{
    count[2]++;
    const uint8_t ifr = via_r(via_2, VIA_IFR) & via_r(via_2, VIA_IER) & 0x7F;
    if (ifr & VIA_IRQ_CA1) {
        vsync_ack();
        via_w(via_2, VIA_IFR, VIA_IRQ_CA1);
        irq_vsync_time = via_clock();   /* 時計を進めておき、時刻を残す */
        irq_vsync_count++;
        beep_vsync();               /* ベルの音の補充 (beep.c) */
    }
    const uint8_t other = ifr & ~VIA_IRQ_CA1;   /* SCSI, ASC, タイマは使っていない */
    if (other)
        via_w(via_2, VIA_IFR, other);
    if (other || !ifr)
        unhandled(2, other);
}

void irq_level3(void)
{
    count[3]++;
    unhandled(3, 0);
}

/* serial.c を取り込まないプログラムでは、この弱い定義 (何もしない) が使われる */
int __attribute__((weak)) serial_irq(void)
{
    return 0;
}

void irq_level4(void)
{
    count[4]++;
    if (serial_irq())
        return;
    (void)scc[SCC_CTRL_A];
    scc_wait();
    const uint8_t rr3 = scc_read_reg(SCC_RR3);
    scc_write_reg(SCC_WR9, 0x00);
    unhandled(4, rr3);
}

void irq_level5(void)
{
    count[5]++;
    unhandled(5, 0);
}

void irq_level6(void)
{
    count[6]++;
    unhandled(6, 0);
}

/* ---- 割り込みの記録 (platform.h) ---- */

const char *platform_irq_name(int level)
{
    static const char *const names[PLATFORM_IRQ_LEVELS] = {
        "spurious",
        "VIA1 (ADB; 1s, 60Hz, timers unused)",
        "VIA2 (vsync; SCSI, ASC, PDS unused)",
        "not connected",
        "SCC (serial, modem port)",
        "not connected",
        "not connected",
        "interrupt button (NMI)",
    };
    return level >= 0 && level < PLATFORM_IRQ_LEVELS ? names[level] : "";
}

uint32_t platform_irq_count(int level)
{
    if (level == 7)
        return trap_nmi_total;      /* trapent.S の trap_nmi_entry が数える */
    return level >= 0 && level < PLATFORM_IRQ_LEVELS ? count[level] : 0;
}

uint32_t platform_irq_unhandled(int level)
{
    return level >= 0 && level < PLATFORM_IRQ_LEVELS ? unhandled_count[level] : 0;
}

uint32_t platform_irq_detail(int level)
{
    return level >= 0 && level < PLATFORM_IRQ_LEVELS ? unhandled_detail[level] : 0;
}

void platform_nmi_stats(uint32_t *presses, uint32_t *last_n, uint32_t *last_us)
{
    *presses = trap_nmi_presses;
    *last_n = trap_nmi_burst_n;
    *last_us = (uint32_t)((uint64_t)trap_nmi_burst_ticks * 1000000u / VIA_HZ);
}
