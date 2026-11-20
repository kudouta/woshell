/*
 * crt0.S - ステージ2のエントリーポイント
 *
 * 割り込みを止めて ROM から制御を取り上げ、.bss をゼロにして main() を呼ぶ。
 * スタックは自分の領域の中 (stage2.ld の __stack_top) に切り替える。ROM が用意したスタックは場所が
 * わからず、ステージ2が大きくなると .bss と重なるおそれがあるため (実機で画面が黒いまま止まったことがある)。
 */
        .section .text.start, "ax"
        .globl  _start
_start:
        ori.w   #0x0700, %sr            /* 割り込み禁止 */
        move.l  %sp, crt0_boot_sp            /* 調査用: ROM が用意したスタックの位置 */
        lea     __stack_top, %sp        /* 自分の領域の中のスタックに切り替える (stage2.ld) */

        lea     __bss_start, %a0
        lea     __bss_end, %a1
1:      cmpa.l  %a1, %a0
        bhs.s   2f
        clr.l   (%a0)+
        bra.s   1b
2:
        jsr     main
3:      bra.s   3b

        .section .data
        .globl  crt0_boot_sp
        .balign 4
crt0_boot_sp:
        .long   0
