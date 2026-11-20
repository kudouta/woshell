/**
 * @file cmds.h
 * @brief シェルの組み込みのコマンド (cmds.c)
 *
 * コマンドの関数は cmd_ で始まる。記号の表には cmd_ を外した名前でも入るので (tools/common/mksymtab.py)、
 * シェルでは help、d 0x80000 のように短い名前で呼べる。
 * 引数はシェルから 32 ビットの整数で渡り、省略した引数は 0 になる。
 */
#ifndef CMDS_H
#define CMDS_H

#include <stdint.h>

/**
 * @brief コマンドの一覧とキーの説明を出す
 * @return 0
 */
int cmd_help(void);

/**
 * @brief 版 (名前とビルドの日時) を出す
 * @return 0
 */
int cmd_version(void);

/**
 * @brief 画面を消す
 * @return 0
 */
int cmd_cls(void);

/**
 * @brief 音を鳴らす
 * @param hz 高さ (Hz)。0 なら 880
 * @param ms 長さ (ミリ秒)。0 なら 120
 * @return 0
 */
int cmd_beep(int hz, int ms);

/**
 * @brief MMU とキャッシュの状態を出す (調査用。何も書き換えない)
 * @param addr 0 ならレジスタと変換の表のあらまし。0 以外なら、その論理番地の変換
 * @return 0
 */
int cmd_mmu(uint32_t addr);

/**
 * @brief 物理 RAM がどこにあるかを調べて出す (調査用。書いた値はすぐ元に戻す)
 * @param start 調べ始める物理番地 (0 なら、使っている RAM (MemTop) の次から)
 * @param end   調べ終える物理番地の次 (0 なら、機種の RAM の空間の終わりまで)
 * @return 0
 */
int cmd_ramscan(uint32_t start, uint32_t end);

/**
 * @brief 入力の履歴を出す
 * @return 0
 */
int cmd_h(void);

/**
 * @brief シェルの変数を出す
 * @return 0
 */
int cmd_vars(void);

/**
 * @brief 垂直同期の回数
 * @return 起動してからの回数
 */
uint32_t cmd_ticks(void);

/**
 * @brief 割り込みのレベルごとの回数、処理する相手がなかった回数と要因、割り込みボタンの記録を出す
 * @return 0
 */
int cmd_irqs(void);

/**
 * @brief 垂直同期を n 回待つ (割り込みボタンで途中で止められる)
 * @param n 回数
 * @return 0
 */
int cmd_delay(int n);

/**
 * @brief 機械を ROM から起動し直す (戻らない)
 * @return 戻らない
 */
int cmd_reboot(void);

/**
 * @brief 時計の日時を出す
 * @return 1904 年 1 月 1 日 0 時からの秒数
 */
uint32_t cmd_date(void);

/**
 * @brief 時計を合わせる (1904-2039 年)
 * @param y, mo, d 年, 月, 日
 * @param h, mi, s 時, 分, 秒
 * @return 0。日時が範囲の外か、書いた値を読み直して違えば -1
 */
int cmd_setdate(int y, int mo, int d, int h, int mi, int s);

/**
 * @brief PRAM を表示する
 * @param addr 最初の番地 (0-255。範囲の外なら 0)
 * @param n    バイト数 (0 か、最後を越えるなら最後まで)
 * @return 0
 */
int cmd_pram(int addr, int n);

/**
 * @brief PRAM を 1 バイト書き換える
 * @param addr 番地 (0-255)
 * @param v    値 (0-255)
 * @return 書いた後に読み直した値。番地か値が範囲の外なら -1
 */
int cmd_pramw(int addr, int v);

/**
 * @brief 使っているシリアルポートと速さ、送受信の記録を出す
 * @return 今の速さ (bps)
 */
int cmd_serial(void);

/**
 * @brief シリアルポートを切り替える (速さはそのまま)
 * @param p 'a' / 'm' / 0 = モデムポート、'b' / 'p' / 1 = プリンタポート
 * @return 切り替えた先 (SERIAL_MODEM か SERIAL_PRINTER)。p が違えば -1
 */
int cmd_port(int p);

/**
 * @brief シリアルの速さを変える
 * @param n 速さ (bps)
 * @return n。使えない速さなら -1
 */
int cmd_baud(int n);

/**
 * @brief 画面に出すものをシリアルにも出すかどうか
 * @param on 1 = 出す、0 = 出さない
 * @return on
 */
int cmd_serout(int on);

/**
 * @brief 画面の読み出し・書き込み・ずらしの時間と、文字を流す速さを測る
 * @param lines 流す行数 (0 なら 200)
 * @return 0
 */
int cmd_scrbench(int lines);

/**
 * @brief メモリを表示する
 * @param addr  最初の番地 (0 なら前の続き。最初は 0x80000)
 * @param n     表示する数 (0 なら 128 バイト分)
 * @param width 1 つの大きさ (1, 2, 4。ほかの値なら前と同じ。最初は 2)
 * @return 0
 */
int cmd_d(uint32_t addr, int n, int width);

/**
 * @brief メモリを書き換える (16 進で入力。Enter で次、'.' で終わり)
 * @param addr  最初の番地
 * @param width 1 つの大きさ (1, 2, 4。ほかの値なら 2)
 * @return 0
 */
int cmd_m(uint32_t addr, int width);

/**
 * @brief メモリの配置を出す
 * @return 0
 */
int cmd_mem(void);

/**
 * @brief メモリを取る (プログラムの後ろから順に切り出すだけ。返さない)
 * @param n バイト数 (16 バイト単位に切り上げる)
 * @return 番地 (16 バイト境界)。足りなければ 0
 */
void *malloc(uint32_t n);

/**
 * @brief 何もしない (malloc は返す仕組みを持たない)
 * @param p malloc が返した番地
 */
void free(void *p);

#endif
