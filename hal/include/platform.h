/**
 * @file platform.h
 * @brief 機種の機能の入口 (機種に依存しない部分は hal/include の入口だけを使う)
 *
 * 機種ごとの実装は hal/<機種>/ にある。機種の定数 (画面の大きさ, 時計の速さ, 名前) は
 * 同じフォルダーの machine.h にあり、Makefile の MACHINE で選ぶ。
 */
#ifndef INCLUDE_PLATFORM_H
#define INCLUDE_PLATFORM_H

#include <stdint.h>
#include "machine.h"

/** 割り込みのレベルの数 (0 = スプリアス ... 7 = 割り込みボタン) */
#define PLATFORM_IRQ_LEVELS (8)

/**
 * @brief 機種を初期化する (例外, 割り込み, 時計, 画面, キーボードの受け取り)
 *
 * main の最初に 1 回呼ぶ。戻ったときには割り込みを受け付けている。
 */
void platform_init(void);

/**
 * @brief RAM の大きさ
 * @return RAM の大きさ (バイト)
 */
uint32_t platform_ram_size(void);

/**
 * @brief 機械を最初から起動し直す (ROM から)。戻らない
 */
void platform_reboot(void) __attribute__((noreturn));

/**
 * @brief 割り込みのレベルの名前 (何の割り込みか)
 * @param level 0 (スプリアス) から 7 (割り込みボタン)
 * @return 名前。範囲の外なら空の文字列
 */
const char *platform_irq_name(int level);

/**
 * @brief 割り込みが入った回数
 * @param level 0 から 7
 * @return 回数。範囲の外なら 0
 */
uint32_t platform_irq_count(int level);

/**
 * @brief 入った割り込みのうち、処理する相手がなかった回数
 * @param level 0 から 7
 * @return 回数。範囲の外なら 0
 */
uint32_t platform_irq_unhandled(int level);

/**
 * @brief 最後に処理する相手がなかったときの、要因のビット (VIA の IFR, SCC の RR3 など)
 * @param level 0 から 7
 * @return 要因のビット。範囲の外なら 0
 */
uint32_t platform_irq_detail(int level);

/**
 * @brief 割り込みボタン (NMI) の記録
 * @param[out] presses 押した回数
 * @param[out] last_n  最後の押下で入った NMI の回数
 * @param[out] last_us 最後の押下で NMI が続いた時間 (マイクロ秒)
 */
void platform_nmi_stats(uint32_t *presses, uint32_t *last_n, uint32_t *last_us);

/**
 * @brief 機械の ROM が起動のときに出すアイコンを読む
 *
 * 絵はプログラムに入れず、動いている機械の ROM から読む。
 * @param[out] bits 32 x 32 の絵 (1 行 1 語、最上位ビットが左端、1 = 黒)
 * @return 読めたら 1。この機種・ROM の版では場所がわからなければ 0
 */
int platform_startup_icon(uint32_t bits[32]);

/**
 * @brief 物理 RAM がどこにあるかを調べて表示する (調査用)
 *
 * 1MB ごとに、RAM があるか、別の番地の写り込みかを調べる。各ブロックで書いた値はすぐ元に戻す。
 * @param start 調べ始める物理番地 (0 なら、使っている RAM (MemTop) の次から)
 * @param end   調べ終える物理番地の次 (0 なら、機種の RAM の空間の終わりまで)
 */
void platform_ram_scan(uint32_t start, uint32_t end);

#endif
