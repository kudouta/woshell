/*
 * boot.S - SE/30 ブートブロック (1024 バイト, ステージ1)
 *
 * ROM はボリューム先頭の 2 ブロックを読み込み、'LK' を確認して
 * bbEntry (オフセット 2) へ飛ぶ。ここでは ROM のディスクドライバを
 * _Read で呼び、C で書いたステージ2を LOAD_ADDR に読み込んで実行する。
 *
 * ステージ2のディスク上の位置とサイズは mkfloppy.py が末尾に書き込む。
 * コードは位置独立 (PC 相対) で、ROM がどこに読み込んでも動く。
 */

/* ローメモリ変数 */
#define ScrnBase    (0x0824)
#define BootDrive   (0x0210)
#define DrvQHead    (0x030A)    /* DrvQHdr + 2 (qHead) */

/* ParamBlockRec (IOParam) のオフセット */
#define ioVRefNum   (22)
#define ioRefNum    (24)
#define ioBuffer    (32)
#define ioReqCount  (36)
#define ioPosMode   (44)
#define ioPosOffset (46)
#define csCode      (26)        /* CntrlParam: Control の命令 */
#define PB_SIZE     (50)

#define SONY_REFNUM (-5)        /* フロッピーのドライバ (.Sony) の参照番号 */
#define EJECT_CODE  (7)         /* .Sony の Control: ディスクを取り出す */

/* DrvQEl のオフセット */
#define dQDrive     (6)
#define dQRefNum    (8)

        .section .text
        .globl  _boot
_boot:
        .word   0x4C4B                  /* bbID = 'LK' */
        bra.w   entry                   /* bbEntry */
        .word   0x4418                  /* bbVersion (System 7 と同じ形式) */
        .word   0                       /* bbPageFlags */
        /* 以下のファイル名は ROM 側が参照した場合に備えた通常値 */
        .org    0x0A
        .byte   6
        .ascii  "System"                /* bbSysName */
        .org    0x1A
        .byte   6
        .ascii  "Finder"                /* bbShellName */
        .org    0x2A
        .byte   7
        .ascii  "MacsBug"               /* bbDbg1Name */
        .org    0x3A
        .byte   12
        .ascii  "Disassembler"          /* bbDbg2Name */
        .org    0x4A
        .byte   13
        .ascii  "StartUpScreen"         /* bbScreenName */
        .org    0x5A
        .byte   6
        .ascii  "Finder"                /* bbHelloName */
        .org    0x6A
        .byte   9
        .ascii  "Clipboard"             /* bbScrapName */
        .org    0x7A
        .word   10                      /* bbCntFCBs */
        .word   20                      /* bbCntEvts */
        .long   0x00004300              /* bb128KSHeap */
        .long   0x00008000              /* bb256KSHeap */
        .long   0x00020000              /* bbSysHeapSize */

        .org    0x8A
entry:
        /* ブートドライブ番号に対応するドライバの参照番号を探す */
        move.w  BootDrive.w, %d1
        movea.l DrvQHead.w, %a1
1:      move.l  %a1, %d0
        beq.w   no_drive
        cmp.w   dQDrive(%a1), %d1
        beq.s   2f
        movea.l (%a1), %a1              /* qLink */
        bra.s   1b
2:      move.w  dQRefNum(%a1), %d2

        /* パラメータブロックを組み立てる (ドライブと参照番号は最初に 1 回だけ入れる) */
        lea     pb(%pc), %a0
        moveq   #(PB_SIZE / 2) - 1, %d0
3:      clr.w   (%a0)+
        dbra    %d0, 3b
        lea     pb(%pc), %a0
        move.w  %d1, ioVRefNum(%a0)
        move.w  %d2, ioRefNum(%a0)

        /* まず全体を 1 回の _Read で読む (速い)。2 回まで試す */
        moveq   #1, %d3
whole:
        lea     pb(%pc), %a0
        move.l  #LOAD_ADDR, ioBuffer(%a0)
        move.l  stage2_size(%pc), ioReqCount(%a0)
        move.w  #1, ioPosMode(%a0)      /* fsFromStart */
        move.l  stage2_offset(%pc), ioPosOffset(%a0)
        .word   0xA002                  /* _Read (同期) */
        tst.w   %d0
        beq.s   loaded
        dbra    %d3, whole

        /* 失敗したら 1 ブロック (512 バイト) ずつ読み直す。それぞれ 4 回まで試し、
           それでもだめなら、どのブロックで失敗したかを画面に出す。
           1 ブロックずつだと回転待ちが入って遅いので、失敗したときだけにする */
        move.l  stage2_size(%pc), %d5
        lsr.l   #8, %d5
        lsr.l   #1, %d5                 /* d5 = ブロック数 */
        moveq   #0, %d4                 /* d4 = 何ブロック目か */
next_block:
        moveq   #3, %d3                 /* d3 = 残りのやり直し回数 */
retry:
        lea     pb(%pc), %a0
        move.l  %d4, %d0
        lsl.l   #8, %d0
        add.l   %d0, %d0                /* d0 = d4 * 512 */
        move.l  %d0, %d1
        add.l   #LOAD_ADDR, %d1
        move.l  %d1, ioBuffer(%a0)
        add.l   stage2_offset(%pc), %d0
        move.l  %d0, ioPosOffset(%a0)
        move.l  #512, ioReqCount(%a0)
        move.w  #1, ioPosMode(%a0)      /* fsFromStart */
        .word   0xA002                  /* _Read (同期) */
        tst.w   %d0
        beq.s   block_ok
        dbra    %d3, retry
        bra.s   read_error
block_ok:
        addq.l  #1, %d4
        cmp.l   %d5, %d4
        blo.s   next_block
loaded:
        /* フロッピーから起動したときは、読み込んだらディスクを取り出す。
           ドライバは読み込みが終わってしばらくすると ROM の割り込みの中でモーターを止めるが、
           ステージ2は最初に割り込みを止めて VIA を設定し直すので、そのままだとモーターが回り続ける
           (実機で、ゲーム中もドライブの音がしていた)。取り出せばモーターも止まる。
           ディスクに書き込む必要ができたら、そのときにもう一度入れてもらう */
        lea     pb(%pc), %a0
        cmp.w   #SONY_REFNUM, ioRefNum(%a0)
        bne.s   13f
        move.w  #EJECT_CODE, csCode(%a0)    /* ioVRefNum = ドライブ番号, ioRefNum = .Sony はそのまま */
        .word   0xA004                  /* _Control (同期) */
13:

        /* 読み込んだのはデータとしての書き込みなので、68030 の命令キャッシュには反映されない。
           古い中身で動かないよう、命令キャッシュとデータキャッシュを無効にしてから飛ぶ
           (CACR のビット3 = 命令キャッシュを空にする, ビット11 = データキャッシュを空にする) */
        movec   %cacr, %d0
        or.w    #0x0808, %d0
        movec   %d0, %cacr

        jmp     LOAD_ADDR

/* ドライブが見つからない: 縦縞で塗って止まる */
no_drive:
        ori.w   #0x0700, %sr
        movea.l ScrnBase.w, %a0
        move.w  #(512 * 342 / 32) - 1, %d1
4:      move.l  #0xAAAAAAAA, (%a0)+
        dbra    %d1, 4b
5:      bra.s   5b

/* 読み込みエラー: 画面の上半分にエラー番号 (OSErr)、下半分に失敗したディスク上のブロック番号を、
   16 ビットの 2 進数で描いて止まる。画面の横幅を 16 マス (1 マス 32 ドット) に分け、左が最上位ビット。
   1 のマスは黒 (左端 2 ドットだけ白い区切り)、0 のマスは白。例えば -81 = $FFAF なら
   上半分は「黒 8, 白, 黒, 白, 黒 4」 */
read_error:
        ori.w   #0x0700, %sr
        move.l  stage2_offset(%pc), %d6
        lsr.l   #8, %d6
        lsr.l   #1, %d6
        add.l   %d4, %d6                /* d6 = ディスク上のブロック番号 */
        movea.l ScrnBase.w, %a0
        move.w  #341, %d1               /* 残りの行 */
6:      move.w  %d0, %d2                /* 上半分: エラー番号 */
        cmp.w   #171, %d1
        bhs.s   7f
        move.w  %d6, %d2                /* 下半分: ブロック番号 */
7:      moveq   #15, %d3
8:      btst    %d3, %d2
        beq.s   9f
        move.l  #0x3FFFFFFF, (%a0)+     /* 1: 黒 (左端 2 ドットは白) */
        bra.s   10f
9:      clr.l   (%a0)+                  /* 0: 白 */
10:     dbra    %d3, 8b
        dbra    %d1, 6b
11:     bra.s   11b

pb:     .space  PB_SIZE

        /* mkfloppy.py が書き換える */
        .org    0x3F8
stage2_offset:  .long   0               /* ディスク上の位置 (バイト) */
stage2_size:    .long   0               /* 読み込むバイト数 (512 の倍数) */
