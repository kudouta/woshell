/**
 * @file serial.c
 * @brief SE/30 のシリアルポート (SCC Z85C30)
 *
 * SCC には 2 つのチャンネルがある (Apple の資料による割り当て):
 *   チャンネル A = モデムポート (SERIAL_MODEM), チャンネル B = プリンタポート (SERIAL_PRINTER)
 * (MAME の macii.cpp では、チャンネル A のつなぎ先の名前が "printer" になっている)
 * 使うのは一度に 1 つ。使っていない方のチャンネルの割り込みは止めておく。
 *
 * SCC の番地はローメモリ変数 SCCRd ($1D8)。1 バイトずつ、B の制御 +0, A の制御 +2, B のデータ +4,
 * A のデータ +6。制御のレジスタは「番号を書いてから読む / 書く」(0 番はそのまま)。
 * WR9 (割り込みの全体の許可とリセット) と RR3 (割り込みの要因) は 2 つのチャンネルで 1 つ
 * (RR3 はチャンネル A の側からだけ読める)。
 * SCC は続けて読み書きすると間に合わないことがあるので (約 2.2us 空ける)、VIA を 2 回読んで待つ。
 *
 * 速さ: 両方のチャンネルの RTxC 端子に 3.6864MHz が入っている (MAME の macii.cpp と同じ)。
 *   57600bps 以下: ボーレート発生器 (BRG) をこのクロックで回し、16 倍のクロックで受ける。
 *                  時定数 = 3686400 / (2 x 16 x 速さ) - 2 (57600 で 0, 9600 で 10)
 *   115200bps: BRG を使わず RTxC を直接 32 倍のクロックとして使う。230400bps は 16 倍。
 *              (MAME は RTxC を直接使う形を再現していないので、MAME では 57600 まで)
 *
 * 割り込み (レベル 4, platform.c から serial_irq を呼ぶ):
 *   受信: 1 文字ごと (受信 FIFO は 3 バイト。115200bps で約 260us 以内に読めばあふれない)
 *   送信: 送信バッファが空いたとき。送るものがなければ「送信割り込みの保留を消す」
 */
#include "serial.h"
#include "mac.h"
#include "via.h"

uint16_t irq_disable(void);         /* platform.c */
void irq_restore(uint16_t sr);

/* チャンネルごとの番地 (SCCRd からの位置) と、RR3 の要因のビット、WR9 のリセットの命令 */
static const struct {
    uint8_t ctrl, data, rx_ip, tx_ip, ext_ip, reset;
} chan[2] = {
    [SERIAL_MODEM]   = { 2, 6, 0x20, 0x10, 0x08, 0x80 },   /* チャンネル A */
    [SERIAL_PRINTER] = { 0, 4, 0x04, 0x02, 0x01, 0x40 },   /* チャンネル B */
};
#define CTRL_A (2)                  /* RR3 を読む側 */

/* RR0 / RR1 のビット */
#define RR0_RX_AVAIL  (0x01)
#define RR0_TX_EMPTY  (0x04)
#define RR1_ALL_SENT  (0x01)
#define RR1_OVERRUN   (0x20)
#define RR1_FRAMING   (0x40)
/* WR0 の命令 */
#define CMD_RESET_EXT    (0x10)
#define CMD_RESET_TX_INT (0x28)
#define CMD_ERROR_RESET  (0x30)
#define CMD_RESET_IUS    (0x38)

#define RX_SIZE (1024)
#define TX_SIZE (4096)

static volatile uint8_t *scc;
static int cur_port = -1;           /* 使っているチャンネル (-1 = なし) */
static uint32_t cur_baud;
static uint8_t ctrl, data;          /* 使っているチャンネルの番地 */
static volatile uint8_t rx_buf[RX_SIZE], tx_buf[TX_SIZE];
static volatile uint16_t rx_head, rx_tail, tx_head, tx_tail;
static volatile struct serial_stats stats;

static void scc_wait(void)
{
    (void)via_r(via_1, VIA_ORB);
    (void)via_r(via_1, VIA_ORB);
}

static void wr_ctrl(int ch, int reg, uint8_t v)
{
    if (reg) {
        scc[ch] = (uint8_t)reg;
        scc_wait();
    }
    scc[ch] = v;
    scc_wait();
}

static uint8_t rd_ctrl(int ch, int reg)
{
    if (reg) {
        scc[ch] = (uint8_t)reg;
        scc_wait();
    }
    const uint8_t v = scc[ch];
    scc_wait();
    return v;
}

/* 使っているチャンネルのレジスタ */
static void wr(int reg, uint8_t v) { wr_ctrl(ctrl, reg, v); }
static uint8_t rd(int reg) { return rd_ctrl(ctrl, reg); }

/* 送信バッファが空いていて送るものがあれば 1 バイト送る (割り込みを止めて呼ぶ) */
static void tx_kick(void)
{
    if (tx_head == tx_tail || !(rd(0) & RR0_TX_EMPTY))
        return;
    scc[data] = tx_buf[tx_tail];
    scc_wait();
    tx_tail = (uint16_t)((tx_tail + 1) % TX_SIZE);
    stats.tx++;
}

int serial_init(int port, uint32_t baud)
{
    uint8_t wr4, wr11, wr14;
    uint16_t tc = 0;
    if (port != SERIAL_MODEM && port != SERIAL_PRINTER)
        return -1;
    if (baud == 115200) {
        wr4 = 0x84;                     /* 32 倍のクロック, ストップビット 1, パリティなし */
        wr11 = 0x00;                    /* 受信・送信のクロック = RTxC */
        wr14 = 0x00;                    /* BRG は止める */
    } else if (baud == 230400) {
        wr4 = 0x44;                     /* 16 倍 */
        wr11 = 0x00;
        wr14 = 0x00;
    } else if (baud >= 300 && baud <= 57600 && 115200 % baud == 0) {
        wr4 = 0x44;                     /* 16 倍 */
        wr11 = 0x50;                    /* 受信・送信のクロック = BRG */
        wr14 = 0x01;                    /* BRG を回す (元のクロック = RTxC) */
        tc = (uint16_t)(115200 / baud - 2);
    } else {
        return -1;
    }

    const uint16_t sr = irq_disable();
    scc = (volatile uint8_t *)SCCRd;
    for (int p = 0; p < 2; p++) {       /* 読むとレジスタの指定が 0 に戻る (途中の状態を捨てる) */
        (void)scc[chan[p].ctrl];
        scc_wait();
    }
    wr_ctrl(chan[SERIAL_MODEM].ctrl, 9, 0x00);      /* MIE = 0: SCC の割り込みをいったん止める */
    for (int p = 0; p < 2; p++)                     /* 両方のチャンネルの割り込みを止める */
        wr_ctrl(chan[p].ctrl, 1, 0x00);
    cur_port = port;
    ctrl = chan[port].ctrl;
    data = chan[port].data;
    wr(9, chan[port].reset);            /* このチャンネルをリセット (WR9 は共通。MIE も 0 のまま) */
    wr(4, wr4);
    wr(1, 0x00);
    wr(3, 0xC0);                        /* 受信 8 ビット (まだ止めておく) */
    wr(5, 0xE2);                        /* 送信 8 ビット, DTR, RTS (まだ止めておく) */
    wr(10, 0x00);                       /* NRZ */
    wr(11, wr11);
    wr(14, 0x00);
    wr(12, (uint8_t)tc);
    wr(13, (uint8_t)(tc >> 8));
    wr(14, wr14);
    wr(15, 0x00);                       /* 外部の状態の割り込みは使わない */
    wr(0, CMD_RESET_EXT);
    wr(0, CMD_RESET_EXT);
    wr(3, 0xC1);                        /* 受信を始める */
    wr(5, 0xEA);                        /* 送信を始める */
    while (rd(0) & RR0_RX_AVAIL) {      /* 残っている受信を捨てる */
        (void)scc[data];
        scc_wait();
    }
    wr(0, CMD_ERROR_RESET);
    rx_head = rx_tail = tx_head = tx_tail = 0;
    wr(1, 0x12);                        /* 1 文字ごとの受信割り込み, 送信割り込み */
    wr(9, 0x08);                        /* MIE: SCC の割り込みを出す */
    cur_baud = baud;
    irq_restore(sr);
    return 0;
}

uint32_t serial_baud(void)
{
    return cur_baud;
}

int serial_port(void)
{
    return cur_port;
}

/* レベル 4 の割り込み (platform.c から)。使っていなければ 0 を返す */
int serial_irq(void)
{
    if (cur_port < 0)
        return 0;
    const uint8_t rx_ip = chan[cur_port].rx_ip, tx_ip = chan[cur_port].tx_ip;
    const uint8_t ext_ip = chan[cur_port].ext_ip, other_ip = chan[1 - cur_port].rx_ip |
        chan[1 - cur_port].tx_ip | chan[1 - cur_port].ext_ip;
    for (int loop = 0; loop < 8; loop++) {
        const uint8_t ip = rd_ctrl(CTRL_A, 3);
        if (!ip)
            break;
        if (ip & rx_ip) {
            while (rd(0) & RR0_RX_AVAIL) {
                const uint8_t r1 = rd(1);
                const uint8_t c = scc[data];
                scc_wait();
                if (r1 & RR1_OVERRUN)
                    stats.rx_overrun++;
                if (r1 & RR1_FRAMING)
                    stats.framing++;
                if (r1 & (RR1_OVERRUN | RR1_FRAMING))
                    wr(0, CMD_ERROR_RESET);
                const uint16_t n = (uint16_t)((rx_head + 1) % RX_SIZE);
                if (n == rx_tail) {
                    stats.rx_overflow++;
                } else {
                    rx_buf[rx_head] = c;
                    rx_head = n;
                }
                stats.rx++;
            }
        }
        if (ip & tx_ip) {
            if (tx_head != tx_tail)
                tx_kick();
            else
                wr(0, CMD_RESET_TX_INT);    /* 送るものがない: 次に書くまで割り込みを出さない */
        }
        if (ip & ext_ip)
            wr(0, CMD_RESET_EXT);
        if (ip & other_ip)                  /* 使っていない方のチャンネル: 割り込みを止める */
            wr_ctrl(chan[1 - cur_port].ctrl, 1, 0x00);
        wr(0, CMD_RESET_IUS);
    }
    return 1;
}

void serial_putc(int c)
{
    if (cur_port < 0)
        return;
    for (;;) {
        const uint16_t sr = irq_disable();
        const uint16_t n = (uint16_t)((tx_head + 1) % TX_SIZE);
        if (n != tx_tail) {
            tx_buf[tx_head] = (uint8_t)c;
            tx_head = n;
            tx_kick();
            irq_restore(sr);
            return;
        }
        tx_kick();                      /* 割り込みが止まっている所から呼ばれても進むように */
        irq_restore(sr);
    }
}

int serial_getc(void)
{
    if (rx_head == rx_tail)
        return -1;
    const uint16_t sr = irq_disable();
    const int c = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_SIZE);
    irq_restore(sr);
    return c;
}

void serial_drain(void)
{
    if (cur_port < 0)
        return;
    for (;;) {
        const uint16_t sr = irq_disable();
        tx_kick();
        const int done = tx_head == tx_tail && (rd(1) & RR1_ALL_SENT);
        irq_restore(sr);
        if (done)
            return;
    }
}

void serial_get_stats(struct serial_stats *s)
{
    s->rx = stats.rx;
    s->tx = stats.tx;
    s->rx_overflow = stats.rx_overflow;
    s->rx_overrun = stats.rx_overrun;
    s->framing = stats.framing;
}
