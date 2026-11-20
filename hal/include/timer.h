/**
 * @file timer.h
 * @brief 時計 (機種に依存しない入口)
 */
#ifndef INCLUDE_TIMER_H
#define INCLUDE_TIMER_H

#include <stdint.h>
#include "machine.h"

/**
 * @brief 増えていく 32 ビットの時計
 *
 * 1 秒に TIMER_HZ (machine.h) 進む。一周すると 0 に戻るので、時間は差 (後 - 前) で測る。
 * @return 今の値
 */
uint32_t timer_now(void);

#endif
