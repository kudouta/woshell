/**
 * @file cpu.h
 * @brief CPU の状態を調べる (機種に依存しないインターフェース。調査用)
 *
 * 実装は CPU ごとのフォルダー (hal/m68030/ など) にある。表示は printf (libc) で行う。
 */
#ifndef INCLUDE_CPU_H
#define INCLUDE_CPU_H

#include <stdint.h>

/**
 * @brief MMU とキャッシュのレジスタと、変換テーブルのあらましを表示する
 *
 * 変換テーブルは読むだけで、何も書き換えない。論理アドレスから物理アドレスへの対応を、続いている範囲ごとにまとめて出す。
 */
void cpu_mmu_report(void);

/**
 * @brief 1 つの論理アドレスが、物理アドレスのどこへ変換されるかを表示する
 * @param addr 論理アドレス
 */
void cpu_mmu_report_addr(uint32_t addr);

/**
 * @brief 物理アドレス paddr を含む 16MB を、変換なしで見えるようにする
 *
 * 論理アドレスの上位 8 ビットが paddr と同じ範囲 (paddr & 0xFF000000 から 16MB) は、変換テーブルを通らず、
 * 論理アドレス = 物理アドレスになる。キャッシュもしない。cpu_phys_window_off で元に戻すまで続くので、
 * その間、同じ範囲にあるプログラムやスタックも変換なしで動く (上位 8 ビットが 0 の範囲では、
 * プログラムのある RAM は元から同じアドレスなので困らない)。調べている間は割り込みを止めておくこと。
 * 68030 では TT1 を使う (元の TT1 の値は覚えておき、off で戻す)。
 * @param paddr 見たい物理アドレス
 */
void cpu_phys_window(uint32_t paddr);

/** @brief cpu_phys_window を解いて、元の変換に戻す */
void cpu_phys_window_off(void);

#endif
