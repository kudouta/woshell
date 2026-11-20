/*
 * vectors.S - 割り込みの入口 (自動ベクタ 24-30)
 *
 * どの入口も、C の呼び出し規約で壊れるレジスタ (d0-d1/a0-a1) を退避して platform.c の関数を呼ぶ。
 * レベル7 (ベクタ 31, 割り込みボタン) は例外と同じ入口 trap_exc_entry (trapent.S) に入る。
 */
        .section .text

        .macro  IRQ_ENTRY name, func
        .globl  \name
\name:
        movem.l %d0-%d1/%a0-%a1, -(%sp)
        jsr     \func
        movem.l (%sp)+, %d0-%d1/%a0-%a1
        rte
        .endm

        IRQ_ENTRY irq_spurious_entry, irq_spurious     /* ベクタ 24: スプリアス割り込み */
        IRQ_ENTRY irq_level1_entry, irq_level1         /* ベクタ 25: VIA1 */
        IRQ_ENTRY irq_level2_entry, irq_level2         /* ベクタ 26: VIA2 */
        IRQ_ENTRY irq_level3_entry, irq_level3         /* ベクタ 27: SE/30 では使われない */
        IRQ_ENTRY irq_level4_entry, irq_level4         /* ベクタ 28: SCC */
        IRQ_ENTRY irq_level5_entry, irq_level5         /* ベクタ 29: SE/30 では使われない */
        IRQ_ENTRY irq_level6_entry, irq_level6         /* ベクタ 30: SE/30 では使われない */
