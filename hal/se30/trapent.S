/*
 * trapent.S - 例外から戻る仕組み (trap.h) の入口とレジスタの保存
 *
 * trap_catch: 呼び出し側が保存を期待するレジスタ (d2-d7, a2-a6, fp2-fp7)、スタックの位置、戻り先を覚える。
 * 例外が起きたら trap_exc_entry -> trap_exc_handler (trap.c) -> trap_resume で、覚えたレジスタとスタックに戻し、
 * trap_catch から例外の番号を返したようにする。
 */
        .section .bss
        .balign 4
trap_ctx:
        .space  44                      /* d2-d7, a2-a6 */
        .space  4                       /* スタックの位置 (戻り先の番地を指す) */
        .space  4                       /* 戻り先 */
        .space  72                      /* fp2-fp7 (12 バイト x 6) */
        .globl  trap_armed, trap_infop
trap_armed:
        .space  4                       /* 1 = 構えている */
trap_infop:
        .space  4                       /* struct trap_info * */

        .section .text
        .globl  trap_catch
        .globl  trap_release
        .globl  trap_resume
        .globl  trap_exc_entry

/* int trap_catch(struct trap_info *info) */
trap_catch:
        move.l  4(%sp), trap_infop
        lea     trap_ctx, %a0
        movem.l %d2-%d7/%a2-%a6, (%a0)
        move.l  %sp, 44(%a0)
        move.l  (%sp), 48(%a0)
        fmovem.x %fp2-%fp7, 52(%a0)
        move.l  #1, trap_armed
        moveq   #0, %d0
        rts

/* void trap_release(void) */
trap_release:
        clr.l   trap_armed
        rts

/* void trap_resume(int vector): trap_catch から vector を返したようにする (戻ってこない) */
trap_resume:
        move.l  4(%sp), %d0
        lea     trap_ctx, %a0
        movem.l (%a0), %d2-%d7/%a2-%a6
        fmovem.x 52(%a0), %fp2-%fp7
        move.l  44(%a0), %sp
        move.l  48(%a0), (%sp)          /* 戻り先 (深い呼び出しで書き換わっているかもしれない) */
        clr.l   trap_armed
        move.w  #0x2000, %sr            /* 割り込みを許可した状態に戻す */
        rts

/* ---- 割り込みボタン (レベル7, ベクタ 31) ----
 * 実機では、押している間ずっと NMI が入り続ける (約 17us ごと = この入口の処理 1 回分。
 * 0.2 秒押すと 1 万回以上)。68030 のレベル7 は「変わったときに 1 回」のはずだが、そうならない。
 * NMI は止められないので、C を呼ばずにここで「同じ 1 回の押下か」を見分け、同じならすぐ戻る。
 * 前の NMI から 50ms 未満なら同じ押下とする。時間は VIA1 タイマ2 の上位バイトだけで測る
 * (1 単位 = 256 カウント = 約 0.33ms, 約 83.7ms で一周)。
 *   下位バイトまで読むと、2 回の読み出しの間に桁が下がったとき 256 カウントずれ、間隔がほぼ 0 の
 *   ときに負 (= 一周近く) に見えて別の押下と数えてしまう (実機で 1/160 ほどの割合で起きた)。
 * 一周を超えたかどうかは垂直同期の回数で見る (3 回以上進んでいれば 50ms 以上たっている)。
 * 新しい押下なら、ふつうの例外の入口 trap_exc_entry に進む (動いている関数を止める)。
 */
        .section .bss
        .balign 4
        .globl  trap_nmi_total, trap_nmi_presses, trap_nmi_burst_n, trap_nmi_burst_ticks
trap_nmi_total:      .space 4                /* NMI が入った回数 */
trap_nmi_presses:    .space 4                /* 押した回数 (50ms 以上あいたら別の押下) */
trap_nmi_burst_n:    .space 4                /* 最後の押下で入った NMI の回数 */
trap_nmi_burst_ticks: .space 4               /* 最後の押下で NMI が続いた時間 (VIA のカウント, 256 単位) */
trap_nmi_last_vsync: .space 4
nmi_last_t2h:   .space 1

        .section .text
        .globl  trap_nmi_entry
        .set    VIA_T2CH, 0x1200
        .set    NMI_GAP, 153            /* 50ms = 39168 カウント = 上位バイトで 153 */
trap_nmi_entry:
        movem.l %d0-%d1/%a0, -(%sp)
        addq.l  #1, trap_nmi_total
        movea.l via_1, %a0
        move.b  VIA_T2CH(%a0), %d0      /* タイマ2 の上位バイト (減っていく) */
        move.b  nmi_last_t2h, %d1
        move.b  %d0, nmi_last_t2h
        sub.b   %d0, %d1                /* d1 = 前の NMI からの時間 (8 ビット, 0.33ms 単位) */
        move.l  irq_vsync_count, %d0
        sub.l   trap_nmi_last_vsync, %d0
        add.l   %d0, trap_nmi_last_vsync     /* trap_nmi_last_vsync = irq_vsync_count */
        tst.l   trap_nmi_presses
        beq.s   2f                      /* 初めての NMI */
        cmpi.l  #3, %d0
        bhs.s   2f                      /* 垂直同期が 3 回以上進んだ: 50ms 以上たっている */
        cmpi.b  #NMI_GAP, %d1
        bhs.s   2f                      /* 50ms 以上あいた */
        /* 同じ押下の続き: 数えてすぐ戻る */
        addq.l  #1, trap_nmi_burst_n
        andi.l  #0xFF, %d1
        lsl.l   #8, %d1
        add.l   %d1, trap_nmi_burst_ticks
        movem.l (%sp)+, %d0-%d1/%a0
        rte
2:      /* 新しい押下 */
        addq.l  #1, trap_nmi_presses
        move.l  #1, trap_nmi_burst_n
        clr.l   trap_nmi_burst_ticks
        movem.l (%sp)+, %d0-%d1/%a0
        bra     trap_exc_entry

/* 例外の入口 (バスエラー, アドレスエラー, 不正命令, ゼロ除算, レベル7 = 割り込みボタン など)。
   trap_exc_handler(frame) が 0 以外を返したら、例外の前に戻る (構えていないときの割り込みボタン)。
   0 なら止まる (構えていないときの例外。trap_exc_handler が画面に知らせてある) */
trap_exc_entry:
        ori.w   #0x0700, %sr
        movem.l %d0-%d1/%a0-%a1, -(%sp)
        pea     16(%sp)                 /* 例外のフレームの先頭 */
        jsr     trap_exc_handler
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        movem.l (%sp)+, %d0-%d1/%a0-%a1
        rte
1:      stop    #0x2700
        bra.s   1b
