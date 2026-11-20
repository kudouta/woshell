/**
 * @file asc.h
 * @brief SE/30 の ASC (Apple Sound Chip) で PCM を鳴らす (SE/30 の中だけで使う)
 *
 * ASC を FIFO モードにし、FIFO に残っている量を時間から計算して、足りない分だけ
 * 登録された関数 (asc_render_fn) にサンプルを作ってもらう。何を鳴らすかには関係しない。
 *
 * サンプルは符号なし 8 ビット ($80 が無音)、約 22,254Hz (15.6672MHz / 704)、モノラル
 * (FIFO A と B に同じ値を書く)。
 */
#ifndef SE30_ASC_H
#define SE30_ASC_H

#include <stdint.h>

/**
 * @brief サンプルを作る関数の形。割り込みの中で呼ばれる
 * @param fa FIFO A (書くと末尾に足される)
 * @param fb FIFO B (同じ値を書く)
 * @param n  作るサンプルの数
 */
typedef void (*asc_render_fn)(volatile uint8_t *fa, volatile uint8_t *fb, uint32_t n);

/**
 * @brief ASC を FIFO モードにし、無音で満たす
 *
 * via_init の後で呼ぶ。補充は asc_poll (垂直同期の割り込みなどから) で行う。
 * @param render サンプルを作る関数
 * @param volume スピーカーへの音量 (0-7)
 */
void asc_init(asc_render_fn render, int volume);

/**
 * @brief スピーカーへの音量を変える
 * @param volume 0-7
 */
void asc_set_volume(int volume);

/**
 * @brief スピーカーへの音量
 * @return 0-7
 */
int asc_volume(void);

/**
 * @brief ASC の割り込み (VIA2 CB1) の処理。FIFO の残りを合わせ直してから補充する
 */
void asc_irq(void);

/**
 * @brief FIFO の補充を確認する。足りない分が 128 以上なら書く
 *
 * ほかの割り込みの中などから、割り込みを止めた状態で呼ぶ。
 */
void asc_poll(void);

/** 調査用: 補充 1 回の最大処理時間 (VIA のカウント) */
extern volatile uint16_t asc_irq_max_ticks;
/** 調査用: 補充の処理時間の合計 (VIA のカウント) */
extern volatile uint32_t asc_irq_total_ticks;

#endif
