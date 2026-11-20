/**
 * @file rtc.h
 * @brief 電池で動く時計と、設定を覚えておく小さなメモリ (PRAM)
 *
 * 時刻は 1904 年 1 月 1 日 0 時からの秒数 (Mac の決まり。時差は持たず、その土地の時刻のまま)。
 * 32 ビットなので 2040 年 2 月 6 日で一周する。
 */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/**
 * @brief 今の時刻を読む
 * @return 1904 年 1 月 1 日 0 時からの秒数
 */
uint32_t rtc_get(void);

/**
 * @brief 時刻を書き込む (書いた後に読み直して確認する)
 * @param secs 1904 年 1 月 1 日 0 時からの秒数
 * @return 読み直して合っていれば 0、違えば -1
 */
int rtc_set(uint32_t secs);

/**
 * @brief PRAM を 1 バイト読む
 * @param addr アドレス (0-255)
 * @return 値
 */
uint8_t rtc_pram_read(int addr);

/**
 * @brief PRAM に 1 バイト書く (Mac OS の設定が変わることがあるので注意)
 * @param addr アドレス (0-255)
 * @param v    値
 */
void rtc_pram_write(int addr, uint8_t v);

#endif
