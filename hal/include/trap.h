/**
 * @file trap.h
 * @brief 例外から戻る仕組み (機種に依存しない入口)
 *
 * 呼んだ関数がバスエラーなどの例外を起こしたり、割り込みボタン (NMI) が押されたりしたら、
 * trap_catch() を呼んだところへ戻る (setjmp / longjmp と同じ考え方)。
 *
 * @code
 *   struct trap_info info;
 *   if (trap_catch(&info) == 0) {
 *       ... 例外を起こすかもしれない処理 ...
 *       trap_release();
 *   } else {
 *       ... info に例外の内容が入っている ...
 *   }
 * @endcode
 *
 * trap_catch を呼んだ関数から戻る前に、必ず trap_release() を呼ぶこと。
 */
#ifndef INCLUDE_TRAP_H
#define INCLUDE_TRAP_H

#include <stdint.h>

/** 起きた例外の内容 */
struct trap_info {
    int vector;             /**< 例外の番号 (2 = バスエラー, 3 = アドレスエラー, 4 = 不正命令, 31 = 割り込みボタン ...) */
    uint32_t pc;            /**< 例外を起こした命令のあたりの番地 */
    uint16_t sr;            /**< そのときの状態レジスタ */
    int has_addr;           /**< 1 なら addr が意味を持つ */
    uint32_t addr;          /**< バスエラー / アドレスエラーで触ろうとした番地 */
};

/**
 * @brief 例外が起きたらここへ戻るように構える
 * @param[out] info 例外で戻ってきたときに、その内容が入る
 * @return 構えたときは 0。例外で戻ってきたときは例外の番号 (1 以上)
 */
int trap_catch(struct trap_info *info) __attribute__((returns_twice));

/**
 * @brief trap_catch の構えを解く
 */
void trap_release(void);

/**
 * @brief 例外の番号の名前
 * @param vector 例外の番号
 * @return 名前 ("bus error" など)
 */
const char *trap_name(int vector);

/**
 * @brief 構えていないときに例外が起きたら呼ばれる (使う側が用意する)
 *
 * 画面に知らせるなどする。戻ると機械は止まる。構えていないときの割り込みボタンは無視され、呼ばれない。
 * @param info 例外の内容
 */
void trap_fatal(const struct trap_info *info);

#endif
