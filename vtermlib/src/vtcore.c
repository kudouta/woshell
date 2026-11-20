/**
 * @file vtcore.c
 * @brief 文字の流れを解釈して、文字の表を書き換える (VT102 と xterm の一部)
 *
 * 解釈は 1 バイトずつ進むステートマシン (DEC の端末の分け方にならって):
 *   S_GROUND   ふつうの文字。表示できる文字は表に書き、制御文字 (0x00-0x1F) は実行する
 *   S_ESC      ESC の次。'[' なら CSI へ、']' 'P' などなら文字列 (読み捨てる) へ
 *   S_ESC_INT  ESC と中間の文字 ('(' ')' '#' など) の次。最後の文字で実行する
 *   S_CSI      CSI (ESC [) の後の引数 (数字と ';')、'?' などの印、中間の文字 (' ' '!' など)
 *   S_CSI_SKIP 形のおかしい CSI。最後の文字まで読み捨てる
 *   S_STR      OSC (ESC ]) / DCS (ESC P) などの文字列。BEL か ESC \ まで読み捨てる
 * どの状態でも、制御文字は (文字列の中を除いて) その場で実行する。CAN と SUB は途中のシーケンスを捨てる。
 *
 * 画面の表 (struct vtcore_screen) は行を map で引くので、スクロールは map を回すだけで済む。
 * 表を変えたら vtcore_mark で行に印を付け、スクロールは vtdraw_scroll で表を動かす前に知らせる。
 */
#include "vtcore.h"
#include "keyboard.h"

struct vtcore_state vtcore_state;
uint8_t vtcore_dirty_row[VTCORE_ROWS];

static struct vtcore_screen screens[2];     /* 0 = 通常の画面, 1 = 代わりの画面 (xterm の ?1049 など) */
static int alt;                         /* 代わりの画面を見せている */

/* モード */
static int m_awm;                       /* DECAWM: 右端で次の行へ折り返す */
static int m_om;                        /* DECOM: 行の番号をスクロール範囲から数える */
static int m_irm;                       /* IRM: 挿入 (書くと右の文字をずらす) */
static int m_lnm;                       /* LNM: LF で行の先頭にも戻る。Return で CR LF を送る */
static int m_ckm;                       /* DECCKM: 矢印のキーで ESC O A などを送る */
static int m_kpam;                      /* DECKPAM: テンキーで ESC O p などを送る */

static int wrap_pending;                /* 右端に書いた直後 (次の文字で折り返す) */
static uint8_t cur_attr;                /* これから書く文字の属性 */
static uint8_t gset[2];                 /* G0, G1 の文字の組 ('B' = ASCII, '0' = 線画, 'A' = 英国) */
static int gl;                          /* 使っている組 (0 = G0, 1 = G1。SI / SO で切り替え) */
static int top, bot;                    /* スクロール範囲 (行, 両端を含む) */
static uint8_t tabs[VTCORE_COLS];           /* タブの位置 */
static int last_ch = -1;                /* 最後に書いた文字 (REP 用) */

/* DECSC で覚えるもの (画面ごと) */
struct saved {
    int valid;
    int x, y, wrap;
    uint8_t attr;
    uint8_t gset[2];
    int gl, om;
};
static struct saved saved[2];

/* ---- 入力の流れ (キーと返事) ---- */
static uint8_t inq[512];
static unsigned inq_head, inq_tail;

static void in_byte(int c)
{
    if (inq_tail - inq_head < sizeof inq)
        inq[inq_tail++ % sizeof inq] = (uint8_t)c;
}

static void in_str(const char *s)
{
    while (*s)
        in_byte(*s++);
}

static void in_num(int n)
{
    char buf[12];
    int i = 0;
    do
        buf[i++] = (char)('0' + n % 10);
    while ((n /= 10) > 0);
    while (i)
        in_byte(buf[--i]);
}

int vtcore_getc(void)
{
    if (inq_head == inq_tail)
        return -1;
    return inq[inq_head++ % sizeof inq];
}

/* ---- 表の操作 ---- */

static int line_cols(int r)
{
    return vtcore_lattr(r) == VTCORE_LINE_NORMAL ? VTCORE_COLS : VTCORE_COLS / 2;
}

static void blank(int r, int x0, int x1)        /* 行 r の [x0, x1) を空白にする */
{
    struct vtcore_cell *l = vtcore_line(r);
    for (int x = x0; x < x1; x++) {
        l[x].ch = ' ';
        l[x].attr = 0;
    }
    vtcore_mark(r);
}

static void set_lattr(int r, int a)
{
    vtcore_state.scr->lattr[vtcore_state.scr->map[r]] = (uint8_t)a;
    vtcore_mark(r);
}

static void clear_line(int r)                   /* 行全体を消し、ふつうの幅に戻す */
{
    blank(r, 0, VTCORE_COLS);
    set_lattr(r, VTCORE_LINE_NORMAL);
}

static void clear_screen(struct vtcore_screen *s)
{
    struct vtcore_screen *keep = vtcore_state.scr;
    vtcore_state.scr = s;
    for (int r = 0; r < VTCORE_ROWS; r++)
        clear_line(r);
    vtcore_state.scr = keep;
}

/* [t, b] を n 行上へ流す。下に空白の行が出てくる */
static void scroll_up(int t, int b, int n)
{
    if (n > b - t + 1)
        n = b - t + 1;
    if (n <= 0)
        return;
    vtdraw_scroll(t, b, n);
    uint8_t *map = vtcore_state.scr->map, tmp[VTCORE_ROWS];
    for (int i = 0; i < n; i++)
        tmp[i] = map[t + i];
    for (int r = t; r <= b - n; r++)
        map[r] = map[r + n];
    for (int i = 0; i < n; i++) {
        map[b - n + 1 + i] = tmp[i];
        clear_line(b - n + 1 + i);
    }
}

/* [t, b] を n 行下へ流す。上に空白の行が出てくる */
static void scroll_down(int t, int b, int n)
{
    if (n > b - t + 1)
        n = b - t + 1;
    if (n <= 0)
        return;
    vtdraw_scroll(t, b, -n);
    uint8_t *map = vtcore_state.scr->map, tmp[VTCORE_ROWS];
    for (int i = 0; i < n; i++)
        tmp[i] = map[b - i];
    for (int r = b; r >= t + n; r--)
        map[r] = map[r - n];
    for (int i = 0; i < n; i++) {
        map[t + n - 1 - i] = tmp[i];
        clear_line(t + n - 1 - i);
    }
}

/* カーソルを 1 行下へ。スクロール範囲の下端なら範囲を流す */
static void index_down(void)
{
    if (vtcore_state.y == bot)
        scroll_up(top, bot, 1);
    else if (vtcore_state.y < VTCORE_ROWS - 1)
        vtcore_state.y++;
}

static void index_up(void)
{
    if (vtcore_state.y == top)
        scroll_down(top, bot, 1);
    else if (vtcore_state.y > 0)
        vtcore_state.y--;
}

/* カーソルを動かした印 (カーソルの描き直しは描く側が位置を比べて行う) */
static void move_to(int x, int y)
{
    if (y < 0)
        y = 0;
    if (y > VTCORE_ROWS - 1)
        y = VTCORE_ROWS - 1;
    vtcore_state.y = y;
    const int cols = line_cols(y);
    if (x < 0)
        x = 0;
    if (x > cols - 1)
        x = cols - 1;
    vtcore_state.x = x;
    wrap_pending = 0;
}

/* 行の番号を DECOM に合わせて決める (CUP, VPA) */
static void move_row(int row, int x)
{
    if (m_om) {
        row += top;
        if (row > bot)
            row = bot;
    }
    move_to(x, row);
}

static void home(void)
{
    move_to(0, m_om ? top : 0);
}

static void put_char(int c)
{
    int g = c;
    const int set = gset[gl];
    if (set == '0' && c >= 0x5F && c <= 0x7E)
        g = FONT_DEC_BASE + c - 0x5F;
    else if (set == 'A' && c == '#')            /* 英国の組: '#' がポンド記号 */
        g = FONT_DEC_BASE + '}' - 0x5F;

    if (wrap_pending) {
        vtcore_state.x = 0;
        index_down();
        wrap_pending = 0;
    }
    const int cols = line_cols(vtcore_state.y);
    if (vtcore_state.x > cols - 1)
        vtcore_state.x = cols - 1;
    struct vtcore_cell *l = vtcore_line(vtcore_state.y);
    if (m_irm)
        for (int x = cols - 1; x > vtcore_state.x; x--)
            l[x] = l[x - 1];
    l[vtcore_state.x].ch = (uint8_t)g;
    l[vtcore_state.x].attr = cur_attr;
    vtcore_mark(vtcore_state.y);
    if (vtcore_state.x == cols - 1) {
        if (m_awm)
            wrap_pending = 1;
    } else {
        vtcore_state.x++;
    }
    last_ch = c;
}

static void tab_forward(int n)
{
    const int cols = line_cols(vtcore_state.y);
    while (n-- > 0 && vtcore_state.x < cols - 1) {
        do
            vtcore_state.x++;
        while (vtcore_state.x < cols - 1 && !tabs[vtcore_state.x]);
    }
    wrap_pending = 0;
}

static void tab_back(int n)
{
    while (n-- > 0 && vtcore_state.x > 0) {
        do
            vtcore_state.x--;
        while (vtcore_state.x > 0 && !tabs[vtcore_state.x]);
    }
    wrap_pending = 0;
}

static void reset_tabs(void)
{
    for (int x = 0; x < VTCORE_COLS; x++)
        tabs[x] = x > 0 && x % 8 == 0;
}

static void save_cursor(void)
{
    struct saved *s = &saved[alt];
    s->valid = 1;
    s->x = vtcore_state.x;
    s->y = vtcore_state.y;
    s->wrap = wrap_pending;
    s->attr = cur_attr;
    s->gset[0] = gset[0];
    s->gset[1] = gset[1];
    s->gl = gl;
    s->om = m_om;
}

static void restore_cursor(void)
{
    struct saved *s = &saved[alt];
    if (!s->valid) {                    /* 覚えていなければ、左上とふつうの文字 */
        cur_attr = 0;
        gset[0] = gset[1] = 'B';
        gl = 0;
        m_om = 0;
        move_to(0, 0);
        return;
    }
    cur_attr = s->attr;
    gset[0] = s->gset[0];
    gset[1] = s->gset[1];
    gl = s->gl;
    m_om = s->om;
    move_to(s->x, s->y);
    wrap_pending = s->wrap;
}

static void use_screen(int which)
{
    if (which == alt)
        return;
    alt = which;
    vtcore_state.scr = &screens[alt];
    vtdraw_dirty_all();
}

/* 状態を最初の形に戻す (RIS)。soft = 1 なら DECSTR (画面とタブはそのまま) */
static void reset(int soft)
{
    m_awm = 1;
    m_om = 0;
    m_irm = 0;
    m_ckm = 0;
    m_kpam = 0;
    cur_attr = 0;
    gset[0] = gset[1] = 'B';
    gl = 0;
    top = 0;
    bot = VTCORE_ROWS - 1;
    vtcore_state.cursor_visible = 1;
    saved[0].valid = saved[1].valid = 0;
    if (soft)
        return;
    m_lnm = 0;
    vtcore_state.cursor_blink = 1;
    vtcore_state.cursor_shape = VTCORE_CURSOR_BLOCK;
    vtcore_state.reverse_screen = 0;
    vtcore_state.smooth_scroll = 0;
    last_ch = -1;
    reset_tabs();
    for (int s = 0; s < 2; s++)
        for (int r = 0; r < VTCORE_ROWS; r++)
            screens[s].map[r] = (uint8_t)r;
    alt = 0;
    vtcore_state.scr = &screens[0];
    clear_screen(&screens[0]);
    clear_screen(&screens[1]);
    move_to(0, 0);
    vtdraw_dirty_all();
}

void vtcore_init(void)
{
    inq_head = inq_tail = 0;
    reset(0);
}

/* ---- 解釈 ---- */

enum { S_GROUND, S_ESC, S_ESC_INT, S_CSI, S_CSI_SKIP, S_STR, S_STR_ESC };
static int state;

#define MAX_PARAMS (16)
static int params[MAX_PARAMS];
static int nparams;                     /* 引数の数 (空の引数も数える) */
static int priv;                        /* CSI の直後の '?' '>' '=' '<' (なければ 0) */
static int inter;                       /* 中間の文字 (0x20-0x2F。なければ 0) */
static int is_osc;                      /* 文字列が OSC (BEL でも終わる) */

/* 引数 i (0 か省略なら def) */
static int arg(int i, int def)
{
    return i < nparams && params[i] > 0 ? params[i] : def;
}

/* 引数 i (省略なら 0) */
static int arg0(int i)
{
    return i < nparams ? params[i] : 0;
}

static void execute(int c)
{
    switch (c) {
    case 0x07:                          /* BEL */
        vt_bell();
        break;
    case 0x08:                          /* BS */
        if (vtcore_state.x > 0)
            vtcore_state.x--;
        wrap_pending = 0;
        break;
    case 0x09:                          /* HT */
        tab_forward(1);
        break;
    case 0x0A: case 0x0B: case 0x0C:    /* LF, VT, FF */
        index_down();
        if (m_lnm)
            vtcore_state.x = 0;
        wrap_pending = 0;
        break;
    case 0x0D:                          /* CR */
        vtcore_state.x = 0;
        wrap_pending = 0;
        break;
    case 0x0E:                          /* SO: G1 を使う */
        gl = 1;
        break;
    case 0x0F:                          /* SI: G0 を使う */
        gl = 0;
        break;
    }
}

static void erase_display(int mode)
{
    switch (mode) {
    case 0:                             /* カーソルから終わりまで */
        blank(vtcore_state.y, vtcore_state.x, VTCORE_COLS);
        for (int r = vtcore_state.y + 1; r < VTCORE_ROWS; r++)
            clear_line(r);
        break;
    case 1:                             /* 始めからカーソルまで */
        for (int r = 0; r < vtcore_state.y; r++)
            clear_line(r);
        blank(vtcore_state.y, 0, vtcore_state.x + 1);
        break;
    case 2:                             /* 全部 */
        for (int r = 0; r < VTCORE_ROWS; r++)
            clear_line(r);
        break;
    }
}

static void erase_line(int mode)
{
    switch (mode) {
    case 0: blank(vtcore_state.y, vtcore_state.x, VTCORE_COLS); break;
    case 1: blank(vtcore_state.y, 0, vtcore_state.x + 1); break;
    case 2: blank(vtcore_state.y, 0, VTCORE_COLS); break;
    }
}

static void insert_chars(int n)
{
    const int cols = line_cols(vtcore_state.y);
    struct vtcore_cell *l = vtcore_line(vtcore_state.y);
    if (n > cols - vtcore_state.x)
        n = cols - vtcore_state.x;
    for (int x = cols - 1; x >= vtcore_state.x + n; x--)
        l[x] = l[x - n];
    blank(vtcore_state.y, vtcore_state.x, vtcore_state.x + n);
    wrap_pending = 0;
}

static void delete_chars(int n)
{
    const int cols = line_cols(vtcore_state.y);
    struct vtcore_cell *l = vtcore_line(vtcore_state.y);
    if (n > cols - vtcore_state.x)
        n = cols - vtcore_state.x;
    for (int x = vtcore_state.x; x < cols - n; x++)
        l[x] = l[x + n];
    blank(vtcore_state.y, cols - n, cols);
    wrap_pending = 0;
}

static void set_line_attr(int a)
{
    set_lattr(vtcore_state.y, a);
    if (a != VTCORE_LINE_NORMAL) {               /* 右半分は見えなくなるので消す */
        blank(vtcore_state.y, VTCORE_COLS / 2, VTCORE_COLS);
        if (vtcore_state.x > VTCORE_COLS / 2 - 1)
            vtcore_state.x = VTCORE_COLS / 2 - 1;
    }
}

static void align_test(void)            /* DECALN: 画面を 'E' で埋める */
{
    top = 0;
    bot = VTCORE_ROWS - 1;
    for (int r = 0; r < VTCORE_ROWS; r++) {
        set_lattr(r, VTCORE_LINE_NORMAL);
        struct vtcore_cell *l = vtcore_line(r);
        for (int x = 0; x < VTCORE_COLS; x++) {
            l[x].ch = 'E';
            l[x].attr = 0;
        }
    }
    move_to(0, 0);
}

static void reply_da(void)
{
    in_str("\033[?6c");                 /* VT102 */
}

static void esc_dispatch(int c)
{
    if (inter == '#') {
        switch (c) {
        case '3': set_line_attr(VTCORE_LINE_DOUBLE_TOP); break;
        case '4': set_line_attr(VTCORE_LINE_DOUBLE_BOTTOM); break;
        case '5': set_line_attr(VTCORE_LINE_NORMAL); break;
        case '6': set_line_attr(VTCORE_LINE_DOUBLE_WIDTH); break;
        case '8': align_test(); break;
        }
        return;
    }
    if (inter == '(' || inter == ')') {
        gset[inter == ')'] = (uint8_t)(c == '0' || c == 'A' ? c : 'B');
        return;
    }
    if (inter)
        return;
    switch (c) {
    case '7': save_cursor(); break;                         /* DECSC */
    case '8': restore_cursor(); break;                      /* DECRC */
    case 'D': index_down(); wrap_pending = 0; break;        /* IND */
    case 'E': vtcore_state.x = 0; index_down(); wrap_pending = 0; break; /* NEL */
    case 'M': index_up(); wrap_pending = 0; break;          /* RI */
    case 'H': tabs[vtcore_state.x] = 1; break;                        /* HTS */
    case 'c': reset(0); break;                              /* RIS */
    case 'Z': reply_da(); break;                            /* DECID */
    case '=': m_kpam = 1; break;                            /* DECKPAM */
    case '>': m_kpam = 0; break;                            /* DECKPNM */
    }
}

static void set_dec_mode(int mode, int on)
{
    switch (mode) {
    case 1: m_ckm = on; break;
    case 4: vtcore_state.smooth_scroll = on; break;
    case 5:
        if (vtcore_state.reverse_screen != on) {
            vtcore_state.reverse_screen = on;
            vtdraw_dirty_all();
        }
        break;
    case 6: m_om = on; home(); break;
    case 7: m_awm = on; if (!on) wrap_pending = 0; break;
    case 12: vtcore_state.cursor_blink = on; break;
    case 25: vtcore_state.cursor_visible = on; break;
    case 47:                            /* 代わりの画面 (消さない) */
        use_screen(on);
        break;
    case 1047:                          /* 代わりの画面。戻るときに代わりの画面を消す */
        if (!on && alt)
            clear_screen(&screens[1]);
        use_screen(on);
        break;
    case 1048:
        if (on)
            save_cursor();
        else
            restore_cursor();
        break;
    case 1049:                          /* カーソルを覚えて、消した代わりの画面へ */
        if (on && !alt) {
            save_cursor();
            use_screen(1);
            clear_screen(&screens[1]);
            /* 代わりの画面のカーソルの記憶は、通常の画面のものを写す (戻るときに使う) */
            saved[1] = saved[0];
        } else if (!on && alt) {
            use_screen(0);
            restore_cursor();
        }
        break;
    }
}

static void sgr(void)
{
    if (nparams == 0) {
        cur_attr = 0;
        return;
    }
    for (int i = 0; i < nparams; i++) {
        const int p = params[i];
        switch (p) {
        case 0: cur_attr = 0; break;
        case 1: cur_attr |= VTCORE_ATTR_BOLD; break;
        case 4: cur_attr |= VTCORE_ATTR_UNDERLINE; break;
        case 5: case 6: cur_attr |= VTCORE_ATTR_BLINK; break;
        case 7: cur_attr |= VTCORE_ATTR_REVERSE; break;
        case 8: cur_attr |= VTCORE_ATTR_INVISIBLE; break;
        case 21: case 22: cur_attr &= ~VTCORE_ATTR_BOLD; break;
        case 24: cur_attr &= ~VTCORE_ATTR_UNDERLINE; break;
        case 25: cur_attr &= ~VTCORE_ATTR_BLINK; break;
        case 27: cur_attr &= ~VTCORE_ATTR_REVERSE; break;
        case 28: cur_attr &= ~VTCORE_ATTR_INVISIBLE; break;
        case 38: case 48: case 58:      /* 色の指定は読み飛ばす (5;n か 2;r;g;b) */
            if (i + 1 < nparams && params[i + 1] == 5)
                i += 2;
            else if (i + 1 < nparams && params[i + 1] == 2)
                i += 4;
            break;
        default:                        /* 2 (薄く), 3 (斜体), 色 (30-49, 90-107) などは白黒の画面では無視する */
            break;
        }
    }
}

static void csi_dispatch(int c)
{
    if (priv == '?') {
        switch (c) {
        case 'h': case 'l':
            for (int i = 0; i < nparams; i++)
                set_dec_mode(params[i], c == 'h');
            break;
        case 'J': erase_display(arg0(0)); break;    /* DECSED (保護の属性はないので ED と同じ) */
        case 'K': erase_line(arg0(0)); break;       /* DECSEL */
        case 'n':
            if (arg0(0) == 6) {                     /* DECXCPR */
                in_str("\033[?");
                in_num(vtcore_state.y + 1 - (m_om ? top : 0));
                in_byte(';');
                in_num(vtcore_state.x + 1);
                in_str("R");
            } else if (arg0(0) == 15) {
                in_str("\033[?13n");                /* プリンターはない */
            }
            break;
        }
        return;
    }
    if (priv == '>') {
        if (c == 'c' && arg0(0) == 0)
            in_str("\033[>1;10;0c");                /* 2 番目の DA: VT220 系として答える */
        return;
    }
    if (priv)
        return;
    if (inter == ' ' && c == 'q') {                 /* DECSCUSR: カーソルの形 */
        const int p = arg0(0);
        vtcore_state.cursor_blink = p == 0 || p % 2 == 1;
        vtcore_state.cursor_shape = p <= 2 ? VTCORE_CURSOR_BLOCK : p <= 4 ? VTCORE_CURSOR_UNDERLINE : VTCORE_CURSOR_BAR;
        return;
    }
    if (inter == '!' && c == 'p') {                 /* DECSTR */
        reset(1);
        return;
    }
    if (inter)
        return;

    const int n = arg(0, 1);
    switch (c) {
    case '@': insert_chars(n); break;                                       /* ICH */
    case 'A': move_to(vtcore_state.x, vtcore_state.y >= top ? (vtcore_state.y - n < top ? top : vtcore_state.y - n) : vtcore_state.y - n); break; /* CUU */
    case 'B': move_to(vtcore_state.x, vtcore_state.y <= bot ? (vtcore_state.y + n > bot ? bot : vtcore_state.y + n) : vtcore_state.y + n); break; /* CUD */
    case 'C': case 'a': move_to(vtcore_state.x + n, vtcore_state.y); break;                     /* CUF, HPR */
    case 'D': move_to(vtcore_state.x - n, vtcore_state.y); break;                               /* CUB */
    case 'E':                                                               /* CNL */
        move_to(0, vtcore_state.y <= bot ? (vtcore_state.y + n > bot ? bot : vtcore_state.y + n) : vtcore_state.y + n);
        break;
    case 'F':                                                               /* CPL */
        move_to(0, vtcore_state.y >= top ? (vtcore_state.y - n < top ? top : vtcore_state.y - n) : vtcore_state.y - n);
        break;
    case 'G': case '`': move_to(n - 1, vtcore_state.y); break;                        /* CHA, HPA */
    case 'H': case 'f': move_row(arg(0, 1) - 1, arg(1, 1) - 1); break;      /* CUP, HVP */
    case 'I': tab_forward(n); break;                                        /* CHT */
    case 'J': erase_display(arg0(0)); break;                                /* ED */
    case 'K': erase_line(arg0(0)); break;                                   /* EL */
    case 'L':                                                               /* IL */
        if (vtcore_state.y >= top && vtcore_state.y <= bot) {
            scroll_down(vtcore_state.y, bot, n);
            move_to(0, vtcore_state.y);
        }
        break;
    case 'M':                                                               /* DL */
        if (vtcore_state.y >= top && vtcore_state.y <= bot) {
            scroll_up(vtcore_state.y, bot, n);
            move_to(0, vtcore_state.y);
        }
        break;
    case 'P': delete_chars(n); break;                                       /* DCH */
    case 'S': scroll_up(top, bot, n); break;                                /* SU */
    case 'T': if (nparams <= 1) scroll_down(top, bot, n); break;            /* SD */
    case 'X': {                                                             /* ECH */
        const int cols = line_cols(vtcore_state.y);
        blank(vtcore_state.y, vtcore_state.x, vtcore_state.x + n > cols ? cols : vtcore_state.x + n);
        wrap_pending = 0;
        break;
    }
    case 'Z': tab_back(n); break;                                           /* CBT */
    case 'b':                                                               /* REP */
        if (last_ch >= 0)
            for (int i = 0; i < n && i < VTCORE_COLS * VTCORE_ROWS; i++)
                put_char(last_ch);
        break;
    case 'c': if (arg0(0) == 0) reply_da(); break;                          /* DA */
    case 'd': move_row(n - 1, vtcore_state.x); break;                                 /* VPA */
    case 'e': move_to(vtcore_state.x, vtcore_state.y + n); break;                               /* VPR */
    case 'g':                                                               /* TBC */
        if (arg0(0) == 0)
            tabs[vtcore_state.x] = 0;
        else if (arg0(0) == 3)
            for (int x = 0; x < VTCORE_COLS; x++)
                tabs[x] = 0;
        break;
    case 'h': case 'l':                                                     /* SM, RM */
        for (int i = 0; i < nparams; i++) {
            if (params[i] == 4)
                m_irm = c == 'h';
            else if (params[i] == 20)
                m_lnm = c == 'h';
        }
        break;
    case 'm': sgr(); break;                                                 /* SGR */
    case 'n':                                                               /* DSR */
        if (arg0(0) == 5) {
            in_str("\033[0n");
        } else if (arg0(0) == 6) {                                          /* CPR */
            in_str("\033[");
            in_num(vtcore_state.y + 1 - (m_om ? top : 0));
            in_byte(';');
            in_num(vtcore_state.x + 1);
            in_byte('R');
        }
        break;
    case 'r': {                                                             /* DECSTBM */
        const int t = arg(0, 1) - 1, b = arg(1, VTCORE_ROWS) - 1;
        if (t < b && b < VTCORE_ROWS) {
            top = t;
            bot = b;
            home();
        }
        break;
    }
    case 's': save_cursor(); break;                                         /* SCOSC */
    case 'u': restore_cursor(); break;                                      /* SCORC */
    case 't':                                                               /* xterm の窓の操作 */
        if (arg0(0) == 18) {                                                /* 文字の数で大きさを答える */
            in_str("\033[8;");
            in_num(VTCORE_ROWS);
            in_byte(';');
            in_num(VTCORE_COLS);
            in_byte('t');
        }
        break;
    case 'x':                                                               /* DECREQTPARM */
        if (arg0(0) <= 1) {
            in_str(arg0(0) == 0 ? "\033[2" : "\033[3");
            in_str(";1;1;112;112;1;0x");    /* パリティなし, 8 ビット, 9600bps, 16x */
        }
        break;
    }
}

static void __attribute__((noinline)) slow_putc(int c);

void vtcore_putc(int c)
{
    /* 近道: ふつうの状態で、ASCII の表示できる文字を行の途中 (右端より前) に書く。いちばん多い場合なので、
       ほかの場合の処理 (大きな関数) を通らずに済ませる */
    if (state == S_GROUND && c >= 0x20 && c < 0x7F && !wrap_pending && !m_irm && gset[gl] == 'B') {
        struct vtcore_screen *s = vtcore_state.scr;
        const int m = s->map[vtcore_state.y];
        if (s->lattr[m] == VTCORE_LINE_NORMAL && vtcore_state.x < VTCORE_COLS - 1) {
            struct vtcore_cell *cell = &s->cell[m][vtcore_state.x++];
            cell->ch = (uint8_t)c;
            cell->attr = cur_attr;
            vtcore_dirty_row[vtcore_state.y] = 1;
            last_ch = c;
            return;
        }
    }
    slow_putc(c);
}

static void slow_putc(int c)
{
    c &= 0xFF;
    /* 文字列 (OSC, DCS など) の中: BEL (OSC のみ) か ESC \ で終わる。ほかは読み捨てる */
    if (state == S_STR || state == S_STR_ESC) {
        if (c == 0x07 && is_osc) {
            state = S_GROUND;
        } else if (c == 0x1B) {
            state = S_STR_ESC;
        } else if (state == S_STR_ESC) {
            state = c == '\\' ? S_GROUND : S_STR;
        } else if (c == 0x18 || c == 0x1A) {
            state = S_GROUND;
        }
        return;
    }
    if (c >= 0x80)                      /* 8 ビットの文字と C1 の制御文字は使わない (7 ビットの端末) */
        return;
    if (c < 0x20 || c == 0x7F) {
        switch (c) {
        case 0x18: case 0x1A:           /* CAN, SUB: 途中のシーケンスを捨てる */
            state = S_GROUND;
            return;
        case 0x1B:                      /* ESC: 新しいシーケンスを始める */
            state = S_ESC;
            inter = 0;
            return;
        case 0x7F:                      /* DEL は何もしない */
            return;
        }
        execute(c);
        return;
    }

    switch (state) {
    case S_GROUND:
        put_char(c);
        break;
    case S_ESC:
        if (c == '[') {
            state = S_CSI;
            nparams = 0;
            priv = 0;
            inter = 0;
            params[0] = 0;
        } else if (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_') {
            state = S_STR;
            is_osc = c == ']';
        } else if (c >= 0x20 && c <= 0x2F) {
            inter = c;
            state = S_ESC_INT;
        } else {
            state = S_GROUND;
            esc_dispatch(c);
        }
        break;
    case S_ESC_INT:
        if (c >= 0x20 && c <= 0x2F)     /* 中間の文字が 2 つ以上: 最初のものだけ覚える */
            break;
        state = S_GROUND;
        esc_dispatch(c);
        break;
    case S_CSI:
        if (c >= '0' && c <= '9') {
            if (inter) {                /* 中間の文字の後に数字は来ない */
                state = S_CSI_SKIP;
                break;
            }
            if (nparams == 0)
                nparams = 1;
            int *p = &params[nparams - 1];
            if (*p < 10000)
                *p = *p * 10 + (c - '0');
        } else if (c == ';' || c == ':') {
            if (inter) {
                state = S_CSI_SKIP;
                break;
            }
            if (nparams == 0)
                nparams = 1;            /* 先頭の空の引数 */
            if (nparams < MAX_PARAMS)
                params[nparams++] = 0;
            /* 多すぎる引数は最後のものに重ねる (使われない) */
        } else if (c >= '<' && c <= '?') {
            if (nparams || priv || inter) {
                state = S_CSI_SKIP;
                break;
            }
            priv = c;
        } else if (c >= 0x20 && c <= 0x2F) {
            if (!inter)
                inter = c;
        } else if (c >= 0x40 && c <= 0x7E) {
            state = S_GROUND;
            csi_dispatch(c);
        }
        break;
    case S_CSI_SKIP:
        if (c >= 0x40 && c <= 0x7E)
            state = S_GROUND;
        break;
    }
}

/* ---- キー -> 入力の流れ ---- */

static void ss3(int c)                  /* ESC O c */
{
    in_str("\033O");
    in_byte(c);
}

static void csi_key(int c)              /* ESC [ c (DECCKM なら ESC O c) */
{
    if (m_ckm) {
        ss3(c);
    } else {
        in_str("\033[");
        in_byte(c);
    }
}

static void tilde_key(int n)            /* ESC [ n ~ */
{
    in_str("\033[");
    in_num(n);
    in_byte('~');
}

void vtcore_key(int key)
{
    if (key < 0)
        return;
    if (key < 0x100) {
        if (key == '\r' && m_lnm)
            in_str("\r\n");
        else
            in_byte(key);
        return;
    }
    if (key & KEYBOARD_KP) {
        const int c = key & 0xFF;
        switch (c) {                    /* VT100 のテンキーにない = / * は PF2-PF4 にする */
        case '=': ss3('Q'); return;
        case '/': ss3('R'); return;
        case '*': ss3('S'); return;
        }
        if (!m_kpam) {
            vtcore_key(c);
            return;
        }
        switch (c) {
        case '-': ss3('m'); break;
        case '+': ss3('l'); break;      /* VT100 のテンキーのコンマの位置 */
        case '.': ss3('n'); break;
        case '\r': ss3('M'); break;     /* Enter */
        default:
            if (c >= '0' && c <= '9')
                ss3('p' + c - '0');
            break;
        }
        return;
    }
    static const uint8_t ftilde[] = { 15, 17, 18, 19, 20, 21, 23, 24 };    /* F5-F12 */
    switch (key) {
    case KEYBOARD_UP: csi_key('A'); break;
    case KEYBOARD_DOWN: csi_key('B'); break;
    case KEYBOARD_RIGHT: csi_key('C'); break;
    case KEYBOARD_LEFT: csi_key('D'); break;
    case KEYBOARD_HOME: csi_key('H'); break;
    case KEYBOARD_END: csi_key('F'); break;
    case KEYBOARD_HELP: tilde_key(2); break;         /* 拡張キーボードの Help の位置は Insert */
    case KEYBOARD_DEL: tilde_key(3); break;
    case KEYBOARD_PGUP: tilde_key(5); break;
    case KEYBOARD_PGDN: tilde_key(6); break;
    case KEYBOARD_CLEAR: ss3('P'); break;            /* PF1 */
    case KEYBOARD_F1: ss3('P'); break;
    case KEYBOARD_F2: ss3('Q'); break;
    case KEYBOARD_F3: ss3('R'); break;
    case KEYBOARD_F4: ss3('S'); break;
    default:
        if (key >= KEYBOARD_F5 && key <= KEYBOARD_F12)
            tilde_key(ftilde[key - KEYBOARD_F5]);
        break;
    }
}
