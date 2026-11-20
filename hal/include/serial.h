/**
 * @file serial.h
 * @brief シリアルポート (機種に依存しない入口)
 *
 * 8 ビット, パリティなし, ストップビット 1。受信は割り込みでバッファにためる。
 * 送信もバッファに入れて割り込みで送る (バッファがいっぱいのときだけ待つ)。
 */
#ifndef INCLUDE_SERIAL_H
#define INCLUDE_SERIAL_H

#include <stdint.h>

#define SERIAL_MODEM   (0)              /**< モデムポート (SE/30 では SCC のチャンネル A) */
#define SERIAL_PRINTER (1)              /**< プリンタポート (SCC のチャンネル B) */

/**
 * @brief ポートと速さを決めて使えるようにする
 *
 * 使うのは一度に 1 つのポートだけで、前に使っていたポートは止める。
 * @param port SERIAL_MODEM か SERIAL_PRINTER
 * @param baud 速さ (bps)
 * @return 0。使えないポートや速さなら -1 (前の設定のまま)
 */
int serial_init(int port, uint32_t baud);

/**
 * @brief 今の速さ
 * @return bps。使っていなければ 0
 */
uint32_t serial_baud(void);

/**
 * @brief 今のポート
 * @return SERIAL_MODEM か SERIAL_PRINTER。使っていなければ -1
 */
int serial_port(void);

/**
 * @brief 1 バイト送る (バッファがいっぱいなら空くまで待つ。使っていなければ何もしない)
 * @param c バイト
 */
void serial_putc(int c);

/**
 * @brief 受け取った 1 バイトを取り出す (待たない)
 * @return バイト。なければ -1
 */
int serial_getc(void);

/** @brief 送り終わるまで待つ */
void serial_drain(void);

/** 送受信の記録 */
struct serial_stats {
    uint32_t rx;                        /**< 受け取ったバイト数 */
    uint32_t tx;                        /**< 送ったバイト数 */
    uint32_t rx_overflow;               /**< 受信のバッファがあふれた回数 */
    uint32_t rx_overrun;                /**< SCC の受信 FIFO があふれた回数 */
    uint32_t framing;                   /**< 枠の誤りの回数 */
};

/**
 * @brief 送受信の記録を読む
 * @param[out] s 記録
 */
void serial_get_stats(struct serial_stats *s);

#endif
