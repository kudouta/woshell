/**
 * @file console.h
 * @brief 文字の画面 (機種に依存しない)
 *
 * VTermLib の端末 (vt.h) の上の薄い層。画面に出すものは VT102 相当の端末を通るので、
 * エスケープシーケンスで色々なことができる (vtermlib/README.md を参照)。
 * 出したものはシリアルにも同じように出す (改行は CR LF。エスケープシーケンスもそのまま送るので、
 * PC の端末にも同じ画面が出る)。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "vtcore.h"

/** 桁数 (6 x 12 の文字なら 85) */
#define CONSOLE_COLS (VTCORE_COLS)

/** @brief 端末を初期化し、画面を消す */
void console_init(void);

/**
 * @brief 1 文字を画面とシリアルに出す
 * @param c 文字。'\\n' は行の先頭へ戻って改行する。エスケープシーケンスもそのまま端末に渡る
 */
void console_putc(int c);

/**
 * @brief 文字列を画面とシリアルに出す
 * @param s 0 で終わる文字列
 */
void console_puts(const char *s);

/** @brief 画面全体を描き直す (画面を直接書き換えた後などに) */
void console_redraw(void);

/** @brief 画面 (とシリアルの端末) を消して、カーソルを左上に置く */
void console_clear(void);

/**
 * @brief カーソルの桁
 * @return 桁 (0 から)
 */
int console_col(void);

/**
 * @brief 画面の行の数
 * @return 行数
 */
int console_rows(void);

/**
 * @brief カーソルを見せる / 隠す (見せている間は点滅する。画面だけで、シリアルには送らない)
 * @param show 1 = 見せる、0 = 隠す
 */
void console_cursor(int show);

/**
 * @brief 画面に出すものをシリアルにも出すかどうか
 * @param on 1 = 出す (最初はこちら。シリアルを使えるようにしてあるときだけ送られる)、0 = 出さない
 */
void console_serial(int on);

/** @brief 変わったところを画面に描く。キーを待つ間などに呼ぶ (出力が続くときは自動でも描く) */
void console_flush(void);

#endif
