/**
 * @file vtcore.h
 * @brief 端末の中身 (文字の表, 状態) と、描く側 (vtdraw.c) との取り決め (VTermLib の中だけで使う)
 *
 * vtcore.c は文字の流れを解釈して文字の表を書き換えるだけで、画面には触らない。
 * 描く側は文字の表を読んで画面を作る。表を書き換えた行には vtcore_mark で印を付け、
 * スクロールは表を動かす前に vtdraw_scroll で描く側に知らせる。
 * PC での確かめ (tests/) では、描く側の代わりに何もしない関数をつなぐ。
 */
#ifndef VTCORE_H
#define VTCORE_H

#include <stdint.h>
#include "machine.h"
#include "font.h"

/** 桁数 (6 x 12 の文字なら 85) */
#define VTCORE_COLS (VIDEO_W / FONT_W)
/** 行数 (6 x 12 の文字なら 28) */
#define VTCORE_ROWS (VIDEO_H / FONT_H)

#if VTCORE_ROWS > 64 || VTCORE_COLS > 255
#error "行は 64 まで、桁は 255 まで (描く側が行の印を 64 ビットに入れているため)"
#endif

/* 文字の属性 (SGR) */
#define VTCORE_ATTR_BOLD      (0x01)    /**< 太字 */
#define VTCORE_ATTR_UNDERLINE (0x02)    /**< 下線 */
#define VTCORE_ATTR_BLINK     (0x04)    /**< 点滅 */
#define VTCORE_ATTR_REVERSE   (0x08)    /**< 反転 */
#define VTCORE_ATTR_INVISIBLE (0x10)    /**< 見えない */

/** 行の属性 (DECSWL, DECDWL, DECDHL) */
enum {
    VTCORE_LINE_NORMAL,                 /**< ふつう */
    VTCORE_LINE_DOUBLE_WIDTH,           /**< 倍幅 */
    VTCORE_LINE_DOUBLE_TOP,             /**< 倍高の上半分 (倍幅も兼ねる) */
    VTCORE_LINE_DOUBLE_BOTTOM,          /**< 倍高の下半分 (倍幅も兼ねる) */
};

/** 1 文字 */
struct vtcore_cell {
    uint8_t ch;                         /**< フォントの文字の番号 (ASCII はそのまま、線画文字は FONT_DEC_BASE から) */
    uint8_t attr;                       /**< VTCORE_ATTR_ の組み合わせ */
};

/** 画面 1 枚 (通常の画面と、xterm の代わりの画面の 2 枚がある)。
    行は map で引く (画面の行 r の中身 = cell[map[r]])。スクロールでは map だけを回す */
struct vtcore_screen {
    struct vtcore_cell cell[VTCORE_ROWS][VTCORE_COLS];
    uint8_t lattr[VTCORE_ROWS];         /**< 行の属性 (cell と同じ番号で引く) */
    uint8_t map[VTCORE_ROWS];           /**< 画面の行 → cell の番号 */
};

/** カーソルの形 (DECSCUSR) */
enum {
    VTCORE_CURSOR_BLOCK,                /**< 四角 */
    VTCORE_CURSOR_UNDERLINE,            /**< 下線 */
    VTCORE_CURSOR_BAR,                  /**< 縦線 */
};

/** 描く側が見る状態 */
struct vtcore_state {
    struct vtcore_screen *scr;          /**< 今見せている画面 */
    int x, y;                           /**< カーソル (0 から) */
    int cursor_visible;                 /**< DECTCEM: カーソルを見せる */
    int cursor_blink;                   /**< カーソルを点滅させる */
    int cursor_shape;                   /**< VTCORE_CURSOR_ */
    int reverse_screen;                 /**< DECSCNM: 画面全体の白黒を反転 */
    int smooth_scroll;                  /**< DECSCLM: スムーズスクロール */
};
/** 端末の状態 (vtcore.c が書き、描く側が読む) */
extern struct vtcore_state vtcore_state;

/** 組み立て直しの要る行の印 (vtcore.c が付け、描く側が消す)。1 文字ごとに付けるので、関数にせず配列に書く */
extern uint8_t vtcore_dirty_row[VTCORE_ROWS];

/**
 * @brief 行に組み立て直しの印を付ける
 * @param r 画面の行 (0 から)
 */
static inline void vtcore_mark(int r)
{
    vtcore_dirty_row[r] = 1;
}

/**
 * @brief 画面の行の文字の並び
 * @param r 画面の行 (0 から)
 * @return VTCORE_COLS 文字の並び
 */
static inline struct vtcore_cell *vtcore_line(int r)
{
    return vtcore_state.scr->cell[vtcore_state.scr->map[r]];
}

/**
 * @brief 画面の行の属性
 * @param r 画面の行 (0 から)
 * @return VTCORE_LINE_ の値
 */
static inline int vtcore_lattr(int r)
{
    return vtcore_state.scr->lattr[vtcore_state.scr->map[r]];
}

/* ---- vtcore.c ---- */

/** @brief 端末の状態を最初の形にし、画面を消す */
void vtcore_init(void);

/**
 * @brief 文字の流れの 1 バイトを解釈する
 * @param c バイト (0x00-0xFF)
 */
void vtcore_putc(int c);

/**
 * @brief キーを、端末が送るバイト列にして入力の流れに入れる
 * @param key keyboard_getc の値 (keyboard.h)
 */
void vtcore_key(int key);

/**
 * @brief 入力の流れ (キーと端末の返事) から 1 バイト取り出す
 * @return バイト。なければ -1
 */
int vtcore_getc(void);

/* ---- 描く側 (vtdraw.c) ---- */

/** @brief 描く側を初期化する (画面を白くする) */
void vtdraw_init(void);

/** @brief 変わったところを画面に反映する (点滅も進める) */
void vtdraw_flush(void);

/**
 * @brief 速さを測る: 全部の行を組み立てる時間と、画面全体を写す時間
 * @param[out] render_ticks 組み立ての時間 (TIMER_HZ の単位)
 * @param[out] write_ticks  写す時間 (TIMER_HZ の単位)
 */
void vtdraw_measure(uint32_t *render_ticks, uint32_t *write_ticks);

/**
 * @brief 組み立てた絵 (shadow) に 1 ビットの絵を重ねる (vt_draw_bitmap の中身)
 * @param x, y 文字の領域の左上からのドット
 * @param w, h 絵の幅と高さ (ドット)
 * @param bits 1 行 (w + 31) / 32 語で h 行、最上位ビットが左端
 */
void vtdraw_bitmap(int x, int y, int w, int h, const uint32_t *bits);

/** @brief 全部描き直す (画面の切り替え, 反転など) */
void vtdraw_dirty_all(void);

/**
 * @brief 範囲を流すことを知らせる。文字の表を動かす「前」に呼ぶ
 *
 * 新しく出てくる行は空白になる。スムーズスクロールのときは、動かし終わるまで戻らない。
 * @param top 範囲の上端の行
 * @param bot 範囲の下端の行 (この行も含む)
 * @param n   流す行数 (正 = 上へ, 負 = 下へ)
 */
void vtdraw_scroll(int top, int bot, int n);

/* ---- vt.c ---- */

/** @brief ベル (BEL, Ctrl-G) を鳴らす */
void vt_bell(void);

#endif
