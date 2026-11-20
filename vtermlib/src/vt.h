/**
 * @file vt.h
 * @brief VT102 相当の文字端末 (VTermLib の入口。機種に依存しない)
 *
 * 画面に出す文字の流れにエスケープシーケンスを混ぜると、カーソル移動、消去、文字の属性、
 * スクロール範囲、倍幅・倍高の行などを指示できる (DEC VT102 と、xterm のよく使われる機能の一部)。
 * 何ができるかは README.md の「対応しているシーケンス」にまとめてある。
 *
 * 端末から見た「ホスト」は、この端末を使うプログラム自身。
 *   - 出力: vt_putc / vt_write / vt_puts (printf も vt_putc を通る)
 *   - 入力: vt_getc。キーボードのキーは端末と同じ形のバイト列になる (矢印 = ESC [ A など)。
 *     端末からの返事 (DA, DSR のカーソル位置など) も同じ流れに入る
 *
 * 画面への反映はまとめて行う (vt_flush)。出力の途中でも約 1/20 秒ごとに自動で反映する。
 * キーを待つ間は vt_getc_wait か、vt_idle を繰り返し呼ぶ (カーソルと文字の点滅もここで進む)。
 */
#ifndef VT_H
#define VT_H

#include <stdint.h>

/**
 * @brief 端末を初期化し、画面を消す
 *
 * platform_init, keyboard_init, beep_init の後に呼ぶ。
 */
void vt_init(void);

/**
 * @brief 端末へ 1 バイト出す (ホストからの文字の流れ)
 * @param c バイト。エスケープシーケンスの一部でもよい
 */
void vt_putc(int c);

/**
 * @brief 端末へ n バイト出す
 * @param s 出すバイトの並び
 * @param n バイト数
 */
void vt_write(const char *s, int n);

/**
 * @brief 端末へ文字列を出す
 * @param s 0 で終わる文字列
 */
void vt_puts(const char *s);

/**
 * @brief '\\n' を "\\r\\n" に直して出すかどうか (Unix の端末の onlcr と同じ)
 * @param on 1 = 直す (最初はこちら)、0 = '\\n' は改行だけ
 */
void vt_set_onlcr(int on);

/** @brief 画面に反映する */
void vt_flush(void);

/** @brief 何もすることがないときに呼ぶ: キーボードを読み、点滅を進め、画面に反映する */
void vt_idle(void);

/**
 * @brief 端末からの入力 (キーと返事) を 1 バイト取り出す (待たない)
 * @return バイト。なければ -1
 */
int vt_getc(void);

/**
 * @brief 端末からの入力を 1 バイト取り出す (来るまで待つ)
 * @return バイト
 */
int vt_getc_wait(void);

/**
 * @brief 画面の桁数 (フォントの大きさで決まる)
 * @return 桁数
 */
int vt_cols(void);

/**
 * @brief 画面の行数 (フォントの大きさで決まる)
 * @return 行数
 */
int vt_rows(void);

/**
 * @brief カーソルの桁 (行の途中かどうかを知るのに使う)
 * @return 桁 (0 から)
 */
int vt_cursor_col(void);

/** @brief 画面を全部描き直す (プログラムが画面 (VRAM) を直接書き換えた後に) */
void vt_redraw(void);

/**
 * @brief 1 ビットの絵を画面に重ねる (点けるだけ。1 = 文字の色)
 *
 * 絵は文字と一緒に流れ、その行に文字を書いたり画面を消したりすると消える (Linux の起動のペンギンと同じ)。
 * 横に文字を並べるときは、文字を先に書いてから絵を描く。
 * @param x, y 文字の領域の左上からのドット
 * @param w, h 絵の幅と高さ (ドット)
 * @param bits 1 行 (w + 31) / 32 語で h 行、最上位ビットが左端
 */
void vt_draw_bitmap(int x, int y, int w, int h, const uint32_t *bits);

#endif
