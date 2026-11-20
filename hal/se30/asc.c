/**
 * @file asc.c
 * @brief SE/30 の ASC (Apple Sound Chip) で PCM を鳴らす (汎用)
 *
 * ASC (MAME の asc.cpp): VIA1 + $14000。$000-$3FF に書くと FIFO A, $400-$7FF は FIFO B の
 * 末尾に追加される。FIFO は 1024 サンプル。$804 (状態) を読むと割り込みが下がる。
 *
 * FIFO の補充: FIFO にどれだけ残っているかを時間から計算し、目標 (ASC_TARGET) に足りない分だけ書く。
 * ASC は 15.6672MHz / 704 (約 22,254Hz) で 1 サンプルずつ消費し、VIA のタイマーは 15.6672MHz / 20 なので、
 * タイマー 35.2 カウント (176/5) で 1 サンプル。状態レジスタのビットだけを頼りに 512 サンプルずつ書くと、
 * 実機では FIFO があふれてサンプルが捨てられ、音が短くなったり乱れたりした (ビットの意味が資料と MAME で違う)。
 * ASC の割り込み (FIFO が半分になった合図) が来たら、残りを 512 とみなして計算を合わせ直す
 * (MAME は 22,257Hz で、実機とわずかにずれるため)。
 */
#include "asc.h"
#include "via.h"

/* ASC のレジスタ */
#define ASC_FIFO_A      (0x000)
#define ASC_FIFO_B      (0x400)
#define ASC_MODE        (0x801)
#define ASC_FIFO_MODE   (0x803)
#define ASC_FIFO_STAT   (0x804)
#define ASC_VOLUME      (0x806)     /* ビット5-7 = スピーカーへの音量 (0-7), ビット2-4 = ASC 内部の音量 */
#define ASC_CLOCK       (0x807)

#define FIFO_HALF       (0x200)
#define ASC_TARGET      (768)       /* FIFO に残しておくサンプル数 (FIFO は 1024) */

static volatile uint8_t *asc;
static asc_render_fn render;
static uint8_t volume;

/* FIFO の残りを計算するための、書いたサンプル数と再生されたサンプル数 */
static uint32_t written, played, frac, last_clock;

volatile uint16_t asc_irq_max_ticks;
volatile uint32_t asc_irq_total_ticks;

void asc_set_volume(int v)
{
    volume = (uint8_t)(v & 7);
    asc[ASC_VOLUME] = (uint8_t)(volume << 5 | 7 << 2);
}

int asc_volume(void)
{
    return volume;
}

void asc_init(asc_render_fn fn, int v)
{
    asc = via_1 + 0x14000;
    render = fn;

    /* 音量。ふだんは Mac OS のサウンドの処理が設定するので、ROM が起動音のあとに残した値のままだと
       鳴らないことがある (実機で確かめた。MAME は音量を再現していない) */
    asc_set_volume(v);

    asc[ASC_MODE] = 1;                  /* FIFO モード */
    asc[ASC_CLOCK] = 0;                 /* 22,257Hz (実機は約 22,254Hz) */
    asc[ASC_FIFO_MODE] = 0x80;          /* FIFO を空にする */
    asc[ASC_FIFO_MODE] = 0x00;

    /* 無音で満たしておく。以後、FIFO の残りを時間から計算して補充する */
    for (int i = 0; i < 0x3FF; i++) {
        asc[ASC_FIFO_A] = 0x80;
        asc[ASC_FIFO_B] = 0x80;
    }
    written = 0x3FF;
    played = 0;
    frac = 0;
    last_clock = via_clock();

    /* 状態レジスタを読んで割り込み信号を戻しておく。
       ASC の割り込み (VIA2 CB1) は使わず、垂直同期の割り込み (約 16.7ms ごと) から asc_poll で補充する
       (beep.c)。FIFO は約 46ms 分あるので間に合う */
    (void)asc[ASC_FIFO_STAT];
}

static void fill(int resync)
{
    uint16_t t0 = via_timer();
    uint8_t stat = asc[ASC_FIFO_STAT];  /* 読むと割り込みが下がる */
    uint32_t now = via_clock();
    frac += (now - last_clock) * 5;
    last_clock = now;
    played += frac / 176;
    frac %= 176;
    if (resync && (stat & 0x01))
        written = played + FIFO_HALF;
    int32_t level = (int32_t)(written - played);
    if (level < 0) {                    /* 補充が間に合わず空になっていた */
        written = played;
        level = 0;
    }
    int32_t want = ASC_TARGET - level;
    if (want < 128)
        return;                         /* まだ足りている (少しずつ書くと呼び出しの手間が増える) */
    render(asc + ASC_FIFO_A, asc + ASC_FIFO_B, (uint32_t)want);
    written += (uint32_t)want;
    uint16_t t = via_timer_since(t0);
    if (t > asc_irq_max_ticks)
        asc_irq_max_ticks = t;
    asc_irq_total_ticks += t;
}

void asc_irq(void)
{
    fill(1);
}

void asc_poll(void)
{
    fill(0);
}
