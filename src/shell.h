/**
 * @file shell.h
 * @brief C の式に似た書き方のシェル (shell.c)
 */
#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include "symtab.h"

/** @brief プロンプトを出して 1 行ずつ読み、実行し続ける (戻らない) */
void shell_run(void) __attribute__((noreturn));

/**
 * @brief 1 行を実行し、結果を出す
 * @param line 0 で終わる 1 行
 */
void shell_exec(const char *line);

/**
 * @brief キーを 1 つ待つ (画面のキーボードとシリアルの両方から。待つ間はカーソルを点滅させる)
 * @return 文字、または KEYBOARD_UP などのキーの値 (keyboard.h)
 */
int shell_getkey(void);

/**
 * @brief 1 行を読む (行の編集と履歴が使える)
 * @param prompt   最初に出す文字列
 * @param[out] buf 読んだ行 (0 で終わる)
 * @param max      読む最大の文字数 (buf は max + 1 バイト要る)
 * @param use_hist 1 なら履歴を使い、読んだ行を履歴に入れる
 * @return 読んだ文字数。Control-C でやめたら -1
 */
int shell_readline(const char *prompt, char *buf, int max, int use_hist);

/** @brief 入力の履歴を出す */
void shell_history(void);

/** @brief シェルの変数を出す */
void shell_vars(void);

/**
 * @brief 番地を含む関数を記号の表から探す (例外の表示などに使う)
 * @param pc 番地
 * @return 番地が pc 以下でいちばん近い関数の記号。プログラムの命令の外なら 0
 */
const struct symtab_entry *shell_sym_near(uint32_t pc);

#endif
