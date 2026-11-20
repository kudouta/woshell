/**
 * @file vtdraw.c
 * @brief 文字の表 (vtcore.c) を画面に描く
 *
 * 画面と同じ形の絵 (shadow, 1 ドット 1 ビット) を RAM に持ち、次の 2 段階で描く。
 *   1. 文字の表で変わった行 (vtcore_dirty_row) を、フォントから shadow に組み立てる
 *   2. shadow で変わった行 (vdirty) を画面 (VRAM) に写す。このとき、カーソルと
 *      画面全体の反転 (DECSCNM) を重ねる (shadow には入れない)
 * VRAM は読むのが遅い (実機で書き込みより遅い) ので、スクロールも shadow の中で動かしてから写す。
 *
 * 行の組み立て (ふつうの幅の行):
 *   文字ごとに「12 行分の形」の場所 (gp) を決め (飾りのある文字は形を作り直して tmpg に置く)、
 *   ドットの 1 行ずつ、GCELLS 文字を GWORDS 語に決まったシフトで詰めていく (6 ドット幅なら 16 文字 = 3 語)。
 *   シフトの量はフォントの幅から決まる定数なので、コンパイラが展開して分岐のない命令の並びにする。
 *   左の余白 (X0) の分は、詰め終えた行を最後にずらす。
 *   倍幅・倍高の行は数が少ないので、1 文字ずつ置く遅い方法で描く。
 *
 * スクロール:
 *   ふつう (ジャンプ): 同じ範囲のスクロールは行数を数えておき (pend)、描くときにまとめて shadow を動かす。
 *       何行流れても、画面に写すのは 1 回の flush に 1 回
 *   スムーズ (DECSCLM): その場で、垂直同期ごとに SMOOTH_STEP ドットずつ shadow を動かして画面に写す。
 *       終わるまで戻らない (本物の VT100 と同じく、出力の速さが落ちる)
 *
 * 文字の飾り:
 *   太字 = 右へ 1 ドットずらして重ねる。下線 = フォントの underline の行を全部点ける。
 *   反転 = ます目の中を白黒反転。点滅 = 約 0.5 秒ごとに文字を消す / 出す。見えない (SGR 8) = 文字を出さない
 *   倍幅の行 = 横を 2 倍に (1 ビットを 2 ビットに広げる表を引く)。倍高の行 = 上半分 / 下半分の行を縦に 2 倍
 */
#define FONT_GLYPHS
#include "vtcore.h"
#include "video.h"
#include "timer.h"

#define X0 ((VIDEO_W - VTCORE_COLS * FONT_W) / 2)   /* 文字の左端 (ドット) */
#define Y0 ((VIDEO_H - VTCORE_ROWS * FONT_H) / 2)   /* 文字の上端 (ドット) */
#define VW (VIDEO_WORDS)

#define SMOOTH_STEP   (2)               /* スムーズスクロールで 1 回の垂直同期に動かすドット数 */
#define BLINK_TICKS   (TIMER_HZ / 2)     /* 文字の点滅 (0.5 秒ごとに出す / 消す) */
#define CURSOR_TICKS  (TIMER_HZ / 2)     /* カーソルの点滅 */

/* 詰め方: GCELLS 文字 (幅 FONT_W) がちょうど GWORDS 語 (32 ビット) になる */
#if FONT_W == 1 || FONT_W == 2 || FONT_W == 4 || FONT_W == 8
#define GCD_W (FONT_W)
#elif FONT_W == 6
#define GCD_W (2)
#else
#define GCD_W (1)
#endif
#define GCELLS (32 / GCD_W)
#define GWORDS (FONT_W / GCD_W)
#define NGROUPS ((VTCORE_COLS + GCELLS - 1) / GCELLS)
#define PACK_WORDS (NGROUPS * GWORDS)           /* 詰めた 1 行の語数 (VW より多いことがある) */

static uint32_t shadow[VIDEO_H][VW];
static uint64_t vdirty;                 /* shadow から画面に写す行 */
static int vfull;                       /* 余白も含めて、画面全体を写す */
static int pend_top, pend_bot, pend_n;  /* まだ shadow を動かしていないスクロール */

static uint16_t dbl[256];               /* 8 ビット -> 各ビットを 2 つにした 16 ビット (倍幅) */
static uint8_t empty[256];              /* 何も点かない文字 */
static const uint8_t cellmask = (uint8_t)(0xFF << (8 - FONT_W));
static const uint8_t blank_glyph[FONT_H];

static int blink_phase = 1;             /* 1 = 点滅する文字を出している */
static int cursor_phase = 1;            /* 1 = 点滅するカーソルを出している */
static uint32_t blink_at, cursor_at;

/* 画面にカーソルを描いてある位置 (row = -1 なら描いていない) */
static struct { int row, x, shape, dw; } drawn = { -1, 0, 0, 0 };

static uint64_t bit(int r)
{
    return (uint64_t)1 << r;
}

/* 行 [t, b] の印 */
static uint64_t range(int t, int b)
{
    return ((b == 63 ? ~(uint64_t)0 : bit(b + 1) - 1)) & ~(bit(t) - 1);
}

void vtdraw_dirty_all(void)
{
    for (int r = 0; r < VTCORE_ROWS; r++)
        vtcore_dirty_row[r] = 1;
    vfull = 1;
    pend_n = 0;                         /* 全部組み立て直すので、動かす必要はない */
}

/* n 語を写す (前から / 後ろから)。4 語ずつ展開する */
static void copy_up(uint32_t *d, const uint32_t *s, int n)
{
    for (; n >= 4; n -= 4) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        d += 4;
        s += 4;
    }
    while (n-- > 0)
        *d++ = *s++;
}

static void copy_down(uint32_t *d, const uint32_t *s, int n)    /* d, s は終わりの次を指す */
{
    for (; n >= 4; n -= 4) {
        d -= 4;
        s -= 4;
        d[3] = s[3]; d[2] = s[2]; d[1] = s[1]; d[0] = s[0];
    }
    while (n-- > 0)
        *--d = *--s;
}

/* shadow の行 [y0, y0 + rows) を dy ドット動かす (dy < 0 = 上へ) */
static void shadow_move(int y0, int rows, int dy)
{
    if (dy < 0)
        copy_up(shadow[y0 + dy], shadow[y0], rows * VW);
    else if (dy > 0)
        copy_down(shadow[y0 + dy + rows], shadow[y0 + rows], rows * VW);
}

static void shadow_clear(int y0, int rows)
{
    uint32_t *p = shadow[y0];
    for (int i = 0; i < rows * VW; i++)
        *p++ = 0;
}

/* 数えておいたスクロールの分だけ shadow を動かす */
static void apply_pending(void)
{
    if (!pend_n)
        return;
    const int h = pend_bot - pend_top + 1;
    const int n = pend_n > 0 ? pend_n : -pend_n;
    if (n < h) {
        const int y_top = Y0 + pend_top * FONT_H;
        if (pend_n > 0)
            shadow_move(y_top + n * FONT_H, (h - n) * FONT_H, -n * FONT_H);
        else
            shadow_move(y_top, (h - n) * FONT_H, n * FONT_H);
    }
    vdirty |= range(pend_top, pend_bot);
    pend_n = 0;
}

/* ---- 行を組み立てる ---- */

/* 飾りの付いた文字の形を作る (ふつうの幅の行。gy = フォントの行) */
static unsigned styled(const uint8_t *g, int attr, int gy)
{
    unsigned bits = 0;
    if (!((attr & VTCORE_ATTR_INVISIBLE) || ((attr & VTCORE_ATTR_BLINK) && !blink_phase))) {
        bits = g[gy];
        if (attr & VTCORE_ATTR_BOLD)
            bits |= bits >> 1;
        if ((attr & VTCORE_ATTR_UNDERLINE) && gy == FONT_UNDERLINE)
            bits = 0xFF;
        bits &= cellmask;
    }
    if (attr & VTCORE_ATTR_REVERSE)
        bits ^= cellmask;
    return bits;
}

/* GCELLS 文字のドットの 1 行を GWORDS 語に詰める。シフトはすべて定数になる */
static inline __attribute__((always_inline))
void pack_group(uint32_t *out, const uint8_t *const *gp, int y)
{
    uint32_t w[GWORDS];
    for (int i = 0; i < GWORDS; i++)
        w[i] = 0;
#pragma GCC unroll 32
    for (int k = 0; k < GCELLS; k++) {
        const uint32_t b = (uint32_t)gp[k][y] << 24;
        const int pos = k * FONT_W, wi = pos >> 5, s = pos & 31;
        w[wi] |= b >> s;
        if (s + FONT_W > 32)
            w[wi + 1] |= b << (32 - s);
    }
    for (int i = 0; i < GWORDS; i++)
        out[i] = w[i];
}

static void render_normal(int r)
{
    static const uint8_t *gp[NGROUPS * GCELLS];
    static uint8_t tmpg[VTCORE_COLS][FONT_H];
    const struct vtcore_cell *l = vtcore_line(r);
    for (int c = 0; c < VTCORE_COLS; c++) {
        const int attr = l[c].attr;
        const uint8_t *g = font_glyph[l[c].ch];
        if (attr) {
            for (int y = 0; y < FONT_H; y++)
                tmpg[c][y] = (uint8_t)styled(g, attr, y);
            g = tmpg[c];
        }
        gp[c] = g;
    }
    for (int c = VTCORE_COLS; c < NGROUPS * GCELLS; c++)
        gp[c] = blank_glyph;

    uint32_t *dst = shadow[Y0 + r * FONT_H];
    for (int y = 0; y < FONT_H; y++, dst += VW) {
        uint32_t row[PACK_WORDS];
        for (int g = 0; g < NGROUPS; g++)
            pack_group(row + g * GWORDS, gp + g * GCELLS, y);
#if X0 == 0
        for (int i = 0; i < VW; i++)
            dst[i] = i < PACK_WORDS ? row[i] : 0;
#else
        uint32_t prev = 0;
        for (int i = 0; i < VW; i++) {
            const uint32_t cur = i < PACK_WORDS ? row[i] : 0;
            dst[i] = cur >> X0 | prev << (32 - X0);
            prev = cur;
        }
#endif
    }
}

/* 倍幅・倍高の行 (遅いが数が少ない) */
static void render_double(int r, int la)
{
    uint32_t *base = shadow[Y0 + r * FONT_H];
    for (int i = 0; i < FONT_H * VW; i++)
        base[i] = 0;
    const struct vtcore_cell *l = vtcore_line(r);
    const int cw = 2 * FONT_W;
    for (int c = 0; c < VTCORE_COLS / 2; c++) {
        const int attr = l[c].attr, ch = l[c].ch;
        if (empty[ch] && !(attr & (VTCORE_ATTR_REVERSE | VTCORE_ATTR_UNDERLINE)))
            continue;
        const uint8_t *g = font_glyph[ch];
        const int x = X0 + c * cw, w = x >> 5, sh = x & 31;
        uint32_t *p = base + w;
        for (int y = 0; y < FONT_H; y++, p += VW) {
            int gy = y;
            if (la == VTCORE_LINE_DOUBLE_TOP)
                gy = y / 2;
            else if (la == VTCORE_LINE_DOUBLE_BOTTOM)
                gy = (y + FONT_H) / 2;
            const unsigned bits = styled(g, attr, gy);
            if (!bits)
                continue;
            const uint32_t v = (uint32_t)dbl[bits] << 16;
            p[0] |= v >> sh;
            if (sh + cw > 32)
                p[1] |= v << (32 - sh);
        }
    }
}

static void render_row(int r)
{
    const int la = vtcore_lattr(r);
    if (la == VTCORE_LINE_NORMAL)
        render_normal(r);
    else
        render_double(r, la);
}

/* ---- 画面に写す ---- */

static int cursor_want(void)
{
    return vtcore_state.cursor_visible && (!vtcore_state.cursor_blink || cursor_phase);
}

/* 画面の行 y0 から rows 行を写す。カーソル (drawn) と反転を重ねる */
static void write_rows(int y0, int rows)
{
    const uint32_t inv = vtcore_state.reverse_screen ? 0xFFFFFFFFu : 0;
    /* カーソルを重ねる範囲 */
    int cy0 = -1, cy1 = -1, cw = 0, cx = 0;
    if (drawn.row >= 0) {
        cw = drawn.dw ? 2 * FONT_W : FONT_W;
        cx = X0 + drawn.x * cw;
        cy0 = Y0 + drawn.row * FONT_H;
        cy1 = cy0 + FONT_H;
        if (drawn.shape == VTCORE_CURSOR_UNDERLINE)
            cy0 = cy1 - 2;
        else if (drawn.shape == VTCORE_CURSOR_BAR)
            cw = drawn.dw ? 2 : 1;
    }
    if (!inv && (cy1 <= y0 || cy0 >= y0 + rows)) {     /* 重ねるものがなければ、まとめて写す */
        video_copy(0, y0, VW, rows, shadow[y0]);
        return;
    }
    const int w = cx >> 5, sh = cx & 31;
    const uint32_t m = cw ? ~(uint32_t)0 << (32 - cw) : 0;    /* 左に cw ビット */
    for (int y = y0; y < y0 + rows; y++) {
        if (!inv && (y < cy0 || y >= cy1)) {
            video_copy(0, y, VW, 1, shadow[y]);
            continue;
        }
        uint32_t row[VW];
        for (int i = 0; i < VW; i++)
            row[i] = shadow[y][i] ^ inv;
        if (y >= cy0 && y < cy1) {
            row[w] ^= m >> sh;
            if (sh + cw > 32)
                row[w + 1] ^= m << (32 - sh);
        }
        video_copy(0, y, VW, 1, row);
    }
}

/* 組み立てと、画面に写すこと。show_cursor = 0 ならカーソルを消す */
static void flush(int show_cursor)
{
    apply_pending();
    for (int r = 0; r < VTCORE_ROWS; r++) {
        if (vtcore_dirty_row[r]) {
            vtcore_dirty_row[r] = 0;
            render_row(r);
            vdirty |= bit(r);
        }
    }
    /* カーソル: 位置や形が変わったら、前の行と今の行を写し直す */
    const int want = show_cursor && cursor_want();
    const int dw = vtcore_lattr(vtcore_state.y) != VTCORE_LINE_NORMAL;
    const int moved = drawn.row != vtcore_state.y || drawn.x != vtcore_state.x || drawn.shape != vtcore_state.cursor_shape || drawn.dw != dw;
    if (drawn.row >= 0 && (!want || moved))
        vdirty |= bit(drawn.row);
    if (want && moved)
        vdirty |= bit(vtcore_state.y);
    if (want) {
        drawn.row = vtcore_state.y;
        drawn.x = vtcore_state.x;
        drawn.shape = vtcore_state.cursor_shape;
        drawn.dw = dw;
    } else {
        drawn.row = -1;
    }
    if (vfull) {
        vfull = 0;
        vdirty = 0;
        write_rows(0, VIDEO_H);
        return;
    }
    for (int r = 0; vdirty; ) {
        /* 続いている行はまとめて写す */
        while (!(vdirty & bit(r)))
            r++;
        int r1 = r;
        while (r1 + 1 < VTCORE_ROWS && (vdirty & bit(r1 + 1)))
            r1++;
        vdirty &= ~range(r, r1);
        write_rows(Y0 + r * FONT_H, (r1 - r + 1) * FONT_H);
        r = r1 + 1;
    }
}

/* 点滅の時刻を見る */
static void tick(void)
{
    const uint32_t now = timer_now();
    if (now - blink_at >= BLINK_TICKS) {
        blink_at = now;
        blink_phase ^= 1;
        for (int r = 0; r < VTCORE_ROWS; r++) {
            const struct vtcore_cell *l = vtcore_line(r);
            for (int c = 0; c < VTCORE_COLS; c++) {
                if (l[c].attr & VTCORE_ATTR_BLINK) {
                    vtcore_dirty_row[r] = 1;
                    break;
                }
            }
        }
    }
    if (now - cursor_at >= CURSOR_TICKS) {
        cursor_at = now;
        cursor_phase ^= 1;
    }
}

void vtdraw_flush(void)
{
    tick();
    flush(1);
}

/* ---- スクロール ---- */

static void smooth(int t, int b, int n)
{
    flush(0);                           /* 今の表を描き終えてから動かす (カーソルは消す) */
    const int y_top = Y0 + t * FONT_H, h = (b - t + 1) * FONT_H;
    const int lines = n > 0 ? n : -n;
    for (int i = 0; i < lines; i++) {
        for (int off = 0; off < FONT_H; off += SMOOTH_STEP) {
            const int s = FONT_H - off < SMOOTH_STEP ? FONT_H - off : SMOOTH_STEP;
            if (n > 0) {
                shadow_move(y_top + s, h - s, -s);
                shadow_clear(y_top + h - s, s);
            } else {
                shadow_move(y_top, h - s, s);
                shadow_clear(y_top, s);
            }
            video_wait_vsync();         /* 表示の切れ目の直後に写す (ビームより先に書き終える) */
            write_rows(y_top, h);
        }
    }
}

void vtdraw_scroll(int t, int b, int n)
{
    if (vtcore_state.smooth_scroll) {
        smooth(t, b, n);
        return;
    }
    const int h = b - t + 1;
    if (pend_n && (t != pend_top || b != pend_bot || (n > 0) != (pend_n > 0)))
        apply_pending();
    pend_top = t;
    pend_bot = b;
    pend_n += n;
    if (pend_n > h)
        pend_n = h;
    if (pend_n < -h)
        pend_n = -h;
    /* 組み立て直しの印も一緒に動かす (shadow の行も後で同じだけ動くので)。新しく出てくる行には印を付ける */
    const int k = n > 0 ? n : -n;
    if (k >= h) {
        for (int r = t; r <= b; r++)
            vtcore_dirty_row[r] = 1;
    } else if (n > 0) {
        for (int r = t; r <= b - k; r++)
            vtcore_dirty_row[r] = vtcore_dirty_row[r + k];
        for (int r = b - k + 1; r <= b; r++)
            vtcore_dirty_row[r] = 1;
    } else {
        for (int r = b; r >= t + k; r--)
            vtcore_dirty_row[r] = vtcore_dirty_row[r - k];
        for (int r = t; r < t + k; r++)
            vtcore_dirty_row[r] = 1;
    }
}

/* shadow に絵を重ねる (vt_draw_bitmap)。文字の領域からはみ出す部分は描かない。
   重ねた行は画面に写し直す印を付ける (組み立て直しの印は付けない。付けると絵が消える) */
void vtdraw_bitmap(int x, int y, int w, int h, const uint32_t *bits)
{
    const int wpr = (w + 31) / 32;      /* 絵の 1 行の語数 */
    for (int r = 0; r < h; r++) {
        const int sy = Y0 + y + r;
        if (y + r < 0 || y + r >= VTCORE_ROWS * FONT_H)
            continue;
        for (int c = 0; c < w; c++) {
            if (!(bits[r * wpr + c / 32] & (0x80000000u >> (c % 32))))
                continue;
            const int sx = X0 + x + c;
            if (x + c < 0 || x + c >= VTCORE_COLS * FONT_W)
                continue;
            shadow[sy][sx >> 5] |= 0x80000000u >> (sx & 31);
        }
        vdirty |= bit((y + r) / FONT_H);
    }
}

/* 速さを測る: 全部の行を組み立てる時間と、画面全体を写す時間 (TIMER_HZ の単位) */
void vtdraw_measure(uint32_t *render_ticks, uint32_t *write_ticks)
{
    flush(1);
    uint32_t t0 = timer_now();
    for (int r = 0; r < VTCORE_ROWS; r++)
        render_row(r);
    uint32_t t1 = timer_now();
    write_rows(0, VIDEO_H);
    uint32_t t2 = timer_now();
    *render_ticks = t1 - t0;
    *write_ticks = t2 - t1;
}

/* ---- 初期化 ---- */

void vtdraw_init(void)
{
    for (int i = 0; i < 256; i++) {
        uint16_t v = 0;
        for (int b = 0; b < 8; b++)
            if (i & (0x80 >> b))
                v |= (uint16_t)(0xC000 >> (2 * b));
        dbl[i] = v;
        int any = 0;
        for (int y = 0; y < FONT_H; y++)
            any |= font_glyph[i][y] & cellmask;
        empty[i] = !any;
    }
    shadow_clear(0, VIDEO_H);
    vtdraw_dirty_all();
    blink_at = cursor_at = timer_now();
}
