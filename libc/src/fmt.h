/**
 * @file fmt.h
 * @brief printf / sprintf (fmt.c)
 *
 * C の標準の名前のまま (命名規則の例外)。使える書式は fmt.c の先頭に書いてある。
 * printf の出力先は、使う側が fmt_putc を用意して決める (画面、シリアルなど)。
 */
#ifndef FMT_H
#define FMT_H

#include <stdarg.h>

/**
 * @brief printf が 1 文字出すときに呼ぶ (使う側が用意する)
 * @param c 文字。エスケープシーケンスの一部でもよい
 */
void fmt_putc(int c);

/**
 * @brief 書式付きで端末に出す
 * @param fmt 書式
 * @return 出した文字数
 */
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief printf の引数を va_list で渡す形
 * @param fmt 書式
 * @param ap  引数
 * @return 出した文字数
 */
int vprintf(const char *fmt, va_list ap);

/**
 * @brief 書式付きで文字列を作る
 * @param[out] buf 書き込み先 (十分な大きさがあること)
 * @param fmt 書式
 * @return 作った文字数 (最後の 0 は数えない)
 */
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief sprintf の引数を va_list で渡す形
 * @param[out] buf 書き込み先
 * @param fmt 書式
 * @param ap  引数
 * @return 作った文字数
 */
int vsprintf(char *buf, const char *fmt, va_list ap);

#endif
