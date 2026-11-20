/**
 * @file irq.h
 * @brief SE/30 の割り込み (SE/30 の中だけで使う)
 *
 * 割り込みの入口 (vectors.S) から呼ばれる処理と、割り込みを止める / 戻す関数。
 * 割り込みの回数などの記録は、機種に依存しない入口 platform.h の platform_irq_* で読む。
 */
#ifndef SE30_IRQ_H
#define SE30_IRQ_H

#include <stdint.h>

/** 垂直同期の割り込みの回数 (video.c が読む) */
extern volatile uint32_t irq_vsync_count;
/** 直前の垂直同期の割り込みが入った時刻 (via_clock の値) */
extern volatile uint32_t irq_vsync_time;

/**
 * @brief 例外と割り込みのベクタを差し替え、SCC の割り込みを止める
 *
 * via_init の後に呼ぶ。割り込みはまだ受け付けない (irq_enable で受け付け始める)。
 */
void irq_init(void);

/**
 * @brief 垂直同期の割り込みを許可し、CPU が割り込みを受け付けるようにする
 *
 * irq_init と、割り込みを使う部品 (ADB など) の初期化の後に呼ぶ。
 */
void irq_enable(void);

/**
 * @brief 割り込みを止める (レベル 7 の割り込みボタン以外)
 * @return 止める前の状態レジスタ (irq_restore に渡す)
 */
uint16_t irq_disable(void);

/**
 * @brief irq_disable の前の状態に戻す
 * @param sr irq_disable が返した値
 */
void irq_restore(uint16_t sr);

/** @brief スプリアス割り込み (ベクタ 24) の処理。vectors.S から呼ばれる */
void irq_spurious(void);
/** @brief レベル1 (VIA1: ADB) の処理。vectors.S から呼ばれる */
void irq_level1(void);
/** @brief レベル2 (VIA2: 垂直同期) の処理。vectors.S から呼ばれる */
void irq_level2(void);
/** @brief レベル3 (つながっていない) の処理。vectors.S から呼ばれる */
void irq_level3(void);
/** @brief レベル4 (SCC: シリアル) の処理。vectors.S から呼ばれる */
void irq_level4(void);
/** @brief レベル5 (つながっていない) の処理。vectors.S から呼ばれる */
void irq_level5(void);
/** @brief レベル6 (つながっていない) の処理。vectors.S から呼ばれる */
void irq_level6(void);

/**
 * @brief シリアルの割り込みの処理 (serial.c)
 *
 * serial.c を取り込まないプログラム (シリアルを使わないもの) では、irq.c の中の何もしない弱い定義が使われる。
 * @return 割り込みを処理したら 1、しなければ 0 (0 なら irq.c が要因を残して SCC の割り込みを止める)
 */
int serial_irq(void);

#endif
