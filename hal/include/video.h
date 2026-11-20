/**
 * @file video.h
 * @brief 画面 (機種に依存しないインターフェース)
 *
 * 画面は 1 ドット 1 ビット (1 = 黒)。1 行は 32 ビットの語の並びで、最上位ビットが左端。
 * 大きさは machine.h の VIDEO_W x VIDEO_H (VIDEO_W は 32 の倍数)。
 */
#ifndef INCLUDE_VIDEO_H
#define INCLUDE_VIDEO_H

#include <stdint.h>
#include "machine.h"

/** 1 行の語数 */
#define VIDEO_WORDS (VIDEO_W / 32)

/**
 * @brief 1 行の一部を書く
 * @param y   行 (0 から)
 * @param w0  書き始める語 (0 から)
 * @param w1  書き終える語 (この語も書く)
 * @param row 1 行分の語の並び。row[w0..w1] を書く
 */
void video_write(int y, int w0, int w1, const uint32_t *row);

/**
 * @brief 長方形を写す
 * @param wx   左端の語 (左端のドットは wx * 32)
 * @param y    上端の行
 * @param nw   幅 (語)
 * @param rows 高さ (行)
 * @param src  nw 語の行が rows 行並んだもの
 */
void video_copy(int wx, int y, int nw, int rows, const uint32_t *src);

/**
 * @brief 画面の中で行をずらす (横は全部)
 * @param dst_y 写す先の行
 * @param src_y 写す元の行
 * @param rows  行数
 */
void video_move_rows(int dst_y, int src_y, int rows);

/**
 * @brief 行を全部読む (読み出しの速さを測るため)
 * @param y0 最初の行
 * @param y1 最後の行の次
 * @return 読んだ値の合計
 */
uint32_t video_read_rows(int y0, int y1);

/**
 * @brief 行を全部同じ値で埋める
 * @param y0 最初の行
 * @param y1 最後の行の次
 * @param v  語の値 (0 = 白, 0xFFFFFFFF = 黒)
 */
void video_fill(int y0, int y1, uint32_t v);

/**
 * @brief これまでの垂直同期の回数
 * @return 回数
 */
uint32_t video_vsync_count(void);

/**
 * @brief 直前の垂直同期の割り込みが入った時刻
 * @return timer_now (timer.h) の値
 */
uint32_t video_vsync_time(void);

/**
 * @brief 次の垂直同期 (画面の表示の切れ目) まで待つ
 * @return その時点までの垂直同期の回数
 */
uint32_t video_wait_vsync(void);

#endif
