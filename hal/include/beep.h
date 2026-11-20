/**
 * @file beep.h
 * @brief 短い音 (機種に依存しない入口)。端末のベル (BEL, Ctrl-G) に使う
 */
#ifndef INCLUDE_BEEP_H
#define INCLUDE_BEEP_H

/**
 * @brief 音を出せるようにする
 *
 * platform_init の後に 1 回呼ぶ。
 */
void beep_init(void);

/**
 * @brief 音を鳴らす (待たずに戻る)
 *
 * 鳴っている途中なら、最初から鳴らし直す。
 * @param hz 音の高さ (Hz)。0 以下なら何もしない
 * @param ms 鳴らす長さ (ミリ秒)。0 以下なら何もしない
 */
void beep_play(int hz, int ms);

/**
 * @brief 音のデータを補充する (機種の部分の垂直同期の割り込みから呼ぶ)
 *
 * beep_init の前に呼ばれても何もしない。
 */
void beep_vsync(void);

#endif
