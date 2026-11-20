/**
 * @file adb.h
 * @brief ADB キーボードの読み取り (VIA1 のシフトレジスタ経由。SE/30 の中だけで使う)
 *
 * adb_start() でキーボードに「レジスタ0 を送れ」(Talk R0) を送り始める。以後は割り込みの中で
 * 応答を受け取るたびに次の問い合わせを送り続け、キーの押下・解放を adb_get_event() 用の待ち行列に積む。
 */
#ifndef SE30_ADB_H
#define SE30_ADB_H

#include <stdint.h>

/** イベントのビット7: 1 = 離した, 0 = 押した */
#define ADB_KEY_UP_FLAG (0x80)

/**
 * @brief VIA1 のシフトレジスタ割り込みを有効にする
 *
 * via_init の後、割り込みを受け付け始める前に呼ぶ。
 */
void adb_init(void);

/**
 * @brief キーボードへの問い合わせを始める (すぐに戻る)
 *
 * 割り込みを受け付け始めた後に一度だけ呼ぶ。
 */
void adb_start(void);

/**
 * @brief 問い合わせが止まっていたら (0.1 秒割り込みが来ない) 送り直す
 *
 * 待ちのループなどで、こまめに呼ぶ。
 */
void adb_watchdog(void);

/** ADB の割り込みの回数 (調査用) */
extern volatile uint32_t adb_activity;

/**
 * @brief キーのイベントを 1 つ取り出す
 * @return ADB のキーコード (ビット 0-6) と ADB_KEY_UP_FLAG。なければ -1
 */
int adb_get_event(void);

/**
 * @brief VIA1 のシフトレジスタ割り込みの処理 (irq.c から呼ばれる)
 */
void adb_irq(void);

#endif
