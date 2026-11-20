/**
 * @file machine.h
 * @brief SE/30 の定数 (hal/include のインターフェースから使う)
 */
#ifndef MACHINE_H
#define MACHINE_H

/** 機種の名前 */
#define MACHINE_NAME "SE/30"

/** 内蔵ディスプレイの幅 (ドット。1bpp) */
#define VIDEO_W (512)
/** 内蔵ディスプレイの高さ (ドット) */
#define VIDEO_H (342)

/** 時計 (timer_now) の 1 秒あたりの進み = VIA のクロック (C7M / 10) */
#define TIMER_HZ (783360u)

#endif
