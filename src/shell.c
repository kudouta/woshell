/**
 * @file shell.c
 * @brief C の式に似た書き方のシェル
 *
 * 1 行が 1 つの文:
 *   式              値を計算して出す       例  3 + 4 * 2      0x1000 >> 4      'A'
 *   名前 = 式       変数に入れる (なければ作る。プログラムの変数ならメモリに書く)
 *   関数 引数, ...  関数を呼ぶ (かっこなしでもよい)   例  d 0x80000, 64      printf "%d\n", 42
 *   関数(引数, ...) 式の中でも呼べる       例  x = strlen("abc") + 1
 * 引数は 10 個まで、すべて 32 ビットの整数として渡す。文字列 "..." はそのアドレスになる。
 * プログラムの関数と変数は、シンボルテーブル (symtab.h) から名前で引く。
 *   関数の名前 -> そのアドレス (呼ぶときはそのアドレスへ)。変数の名前 -> その中身 (32 ビット)。&名前 -> アドレス
 *   *式 -> そのアドレスの 32 ビット (読み書きできる)
 * 演算子は C と同じ優先順位: * / %  + -  << >>  < <= > >=  == !=  &  ^  |  &&  ||  = と、単項の - ~ ! * &
 *
 * 呼んだ関数でバスエラーなどの例外が起きたり、割り込みボタンが押されたりしたら、プロンプトに戻る (trap.h)。
 */
#include <stdint.h>
#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "vt.h"
#include "serial.h"
#include "fmt.h"
#include "symtab.h"
#include "trap.h"

#define PROMPT     "-> "
#define LINE_MAX   (CONSOLE_COLS - 4)  /* 1 行に収まる長さ (行をまたぐ編集はしない。81 文字) */
#define HIST_N     (16)
#define NAME_MAX   (31)
#define VARS_N     (64)
#define MAX_ARGS   (10)
#define POOL_SIZE  (8192)

/* ---- 入力 ---- */

/* 端末から来たバイト列をキーの値に直す。画面のキーボード (VTermLib が端末と同じ形のバイト列にする) と
   シリアル (PC の端末) の 2 つがあるので、それぞれに 1 つずつ状態を持つ。
   Enter は '\r' (後ろに '\n' が付くこともあるので、'\r' の直後の '\n' は捨てる)、
   後退は 0x7F か '\b'、矢印などはエスケープシーケンス (ESC [ A, ESC O A, ESC [ 3 ~ など)。
   知らないシーケンス (F キー、端末からの返事など) は最後の文字まで読んで捨てる。
   Esc を単独で押すと、次の文字まで待つ (シェルでは Esc を使わない) */
struct shell_keyparse {
    int state;                          /* 0 = ふつう, 1 = ESC の後, 2 = ESC [ / ESC O の後 */
    int num;                            /* ESC [ の後の数 */
    int last_cr;
};

static int parse_key(struct shell_keyparse *p, int c)
{
    const int was_cr = p->last_cr;
    p->last_cr = c == '\r';
    switch (p->state) {
    case 1:                             /* ESC の後 */
        p->state = c == '[' || c == 'O' ? 2 : 0;
        p->num = 0;
        return -1;
    case 2:                             /* ESC [ の後: 数と ';' を読み、最後の文字で決める */
        if ((c >= '0' && c <= '9') || c == ';') {
            if (c != ';')
                p->num = p->num * 10 + (c - '0');
            return -1;
        }
        if (c < 0x40 || c > 0x7E)       /* 最後の文字ではない (中間の文字など) */
            return -1;
        p->state = 0;
        switch (c) {
        case 'A': return KEYBOARD_UP;
        case 'B': return KEYBOARD_DOWN;
        case 'C': return KEYBOARD_RIGHT;
        case 'D': return KEYBOARD_LEFT;
        case 'H': return KEYBOARD_HOME;
        case 'F': return KEYBOARD_END;
        case '~':
            switch (p->num) {
            case 1: case 7: return KEYBOARD_HOME;
            case 4: case 8: return KEYBOARD_END;
            case 3: return KEYBOARD_DEL;
            }
            return -1;
        default:
            return -1;
        }
    }
    if (c == 0x1B) {
        p->state = 1;
        return -1;
    }
    if (c == 0x7F)
        return '\b';
    if (c == '\n')
        return was_cr ? -1 : '\r';
    return c;
}

/* キーを 1 つ待つ。画面のキーボードとシリアルの両方から受け取る */
int shell_getkey(void)
{
    static struct shell_keyparse from_screen, from_serial;
    console_cursor(1);
    int c;
    for (;;) {
        if ((c = vt_getc()) >= 0 && (c = parse_key(&from_screen, c)) >= 0)
            break;
        if ((c = serial_getc()) >= 0 && (c = parse_key(&from_serial, c)) >= 0)
            break;
    }
    console_cursor(0);
    return c;
}

static char hist[HIST_N][LINE_MAX + 1];
static int hist_n;                      /* これまでに入れた数 */

static void hist_add(const char *s)
{
    if (!*s)
        return;
    if (hist_n > 0) {                   /* 直前と同じなら入れない */
        const char *last = hist[(hist_n - 1) % HIST_N];
        int i = 0;
        while (s[i] && s[i] == last[i])
            i++;
        if (s[i] == last[i])
            return;
    }
    char *d = hist[hist_n % HIST_N];
    int i = 0;
    for (; s[i] && i < LINE_MAX; i++)
        d[i] = s[i];
    d[i] = 0;
    hist_n++;
}

void shell_history(void)
{
    const int first = hist_n > HIST_N ? hist_n - HIST_N : 0;
    for (int i = first; i < hist_n; i++)
        printf("%4d  %s\n", i + 1, hist[i % HIST_N]);
}

/* 行の編集。表示は「左へ戻る ('\b')」と上書きでする */
struct edit {
    char *buf;
    int len, pos, max;
};

static void back(int n)
{
    while (n-- > 0)
        console_putc('\b');
}

/* pos から後ろを書き直し、消えた分 (erase 文字) を空白で消して、カーソルを pos に戻す */
static void redraw_tail(struct edit *e, int erase)
{
    for (int i = e->pos; i < e->len; i++)
        console_putc(e->buf[i]);
    for (int i = 0; i < erase; i++)
        console_putc(' ');
    back(e->len - e->pos + erase);
}

static void set_line(struct edit *e, const char *s)
{
    back(e->pos);
    const int old = e->len;
    e->len = e->pos = 0;
    while (s[e->len] && e->len < e->max)
        e->buf[e->len] = s[e->len], e->len++;
    e->buf[e->len] = 0;
    redraw_tail(e, old > e->len ? old - e->len : 0);
    for (; e->pos < e->len; e->pos++)
        console_putc(e->buf[e->pos]);
}

int shell_readline(const char *prompt, char *buf, int max, int use_hist)
{
    struct edit e = { buf, 0, 0, max };
    int hpos = hist_n;                  /* 履歴の中の位置 (hist_n = 今の行) */
    static char saved[LINE_MAX + 1];    /* 履歴をさかのぼる前の行 */
    buf[0] = 0;
    if (console_col() != 0)
        console_putc('\n');
    console_puts(prompt);
    for (;;) {
        const int c = shell_getkey();
        switch (c) {
        case '\r': case '\n':
            buf[e.len] = 0;
            for (; e.pos < e.len; e.pos++)
                console_putc(buf[e.pos]);
            console_putc('\n');
            if (use_hist)
                hist_add(buf);
            return e.len;
        case 3:                         /* Control-C: やめる */
            console_puts("^C\n");
            buf[0] = 0;
            return -1;
        case '\b': case 0x7F:
            if (e.pos > 0) {
                for (int i = e.pos - 1; i < e.len; i++)
                    buf[i] = buf[i + 1];
                e.pos--;
                e.len--;
                console_putc('\b');
                redraw_tail(&e, 1);
            }
            break;
        case KEYBOARD_DEL: case 4:           /* Delete (前へ消す) / Control-D */
            if (e.pos < e.len) {
                for (int i = e.pos; i < e.len; i++)
                    buf[i] = buf[i + 1];
                e.len--;
                redraw_tail(&e, 1);
            }
            break;
        case KEYBOARD_LEFT: case 2:          /* Control-B */
            if (e.pos > 0) {
                e.pos--;
                console_putc('\b');
            }
            break;
        case KEYBOARD_RIGHT: case 6:         /* Control-F */
            if (e.pos < e.len)
                console_putc(buf[e.pos++]);
            break;
        case KEYBOARD_HOME: case 1:          /* Control-A */
            back(e.pos);
            e.pos = 0;
            break;
        case KEYBOARD_END: case 5:           /* Control-E */
            for (; e.pos < e.len; e.pos++)
                console_putc(buf[e.pos]);
            break;
        case 21: {                      /* Control-U: 行を消す */
            set_line(&e, "");
            break;
        }
        case 11: {                      /* Control-K: カーソルから後ろを消す */
            const int n = e.len - e.pos;
            e.len = e.pos;
            buf[e.len] = 0;
            redraw_tail(&e, n);
            break;
        }
        case 12: {                      /* Control-L: 画面を消して書き直す */
            console_clear();
            console_puts(prompt);
            const int p = e.pos;
            e.pos = 0;
            redraw_tail(&e, 0);
            for (; e.pos < p; e.pos++)
                console_putc(buf[e.pos]);
            break;
        }
        case KEYBOARD_UP: case 16:           /* Control-P: 前の履歴 */
            if (use_hist && hpos > 0 && hpos > hist_n - HIST_N) {
                if (hpos == hist_n) {
                    for (int i = 0; i <= e.len; i++)
                        saved[i] = buf[i];
                }
                hpos--;
                set_line(&e, hist[hpos % HIST_N]);
            }
            break;
        case KEYBOARD_DOWN: case 14:         /* Control-N: 次の履歴 */
            if (use_hist && hpos < hist_n) {
                hpos++;
                set_line(&e, hpos == hist_n ? saved : hist[hpos % HIST_N]);
            }
            break;
        default:
            if (c >= 0x20 && c <= 0x7E && e.len < e.max) {
                for (int i = e.len; i >= e.pos; i--)
                    buf[i + 1] = buf[i];
                buf[e.pos] = (char)c;
                e.len++;
                console_putc(c);
                e.pos++;
                redraw_tail(&e, 0);
            }
            break;
        }
    }
}

/* ---- 変数と文字列 ---- */

static struct { char name[NAME_MAX + 1]; int32_t value; } vars[VARS_N];
static int n_vars;

void shell_vars(void)
{
    for (int i = 0; i < n_vars; i++)
        printf("%-16s = %d = 0x%x\n", vars[i].name, (int)vars[i].value, (unsigned)vars[i].value);
}

/* 文字列は使い回す場所に置く (古いものから上書きされる) */
static char pool[POOL_SIZE];
static int pool_pos;

static char *pool_alloc(int n)
{
    if (pool_pos + n > POOL_SIZE)
        pool_pos = 0;
    char *p = pool + pool_pos;
    pool_pos += n;
    return p;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static void str_copy(char *d, const char *s, int max)
{
    int i = 0;
    for (; s[i] && i < max; i++)
        d[i] = s[i];
    d[i] = 0;
}

static const struct symtab_entry *sym_find(const char *name)
{
    int lo = 0, hi = symtab_count - 1;     /* 表は名前の順 */
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const char *a = symtab_table[mid].name, *b = name;
        while (*a && *a == *b)
            a++, b++;
        const int d = (unsigned char)*a - (unsigned char)*b;
        if (d == 0)
            return &symtab_table[mid];
        if (d < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* pc を含む関数 (アドレスが pc 以下で一番近い 'T' のシンボル)。プログラムの命令の外なら 0 */
const struct symtab_entry *shell_sym_near(uint32_t pc)
{
    extern char __text_end[];           /* stage2.ld */
    const struct symtab_entry *best = 0;
    if (pc >= (uint32_t)__text_end)
        return 0;
    for (int i = 0; i < symtab_count; i++) {
        const struct symtab_entry *s = &symtab_table[i];
        if (s->type == 'T' && s->addr <= pc && (!best || s->addr > best->addr))
            best = s;
    }
    return best;
}

/* ---- 字句 ---- */

enum { T_END, T_NUM, T_STR, T_IDENT, T_OP };
/* 2 文字の演算子 */
enum { OP_SHL = 256, OP_SHR, OP_LE, OP_GE, OP_EQ, OP_NE, OP_LAND, OP_LOR };

static struct {
    const char *p;                      /* 読んでいる位置 */
    int type, op;
    int32_t num;
    char *str;
    char name[NAME_MAX + 1];
} lx;

static void *err_jmp[5];                /* __builtin_setjmp の置き場所 */
static const char *err_msg;
static char err_buf[64];

static void fail(const char *msg) __attribute__((noreturn));
static void fail(const char *msg)
{
    err_msg = msg;
    __builtin_longjmp(err_jmp, 1);
}

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int hex_val(int c)
{
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 文字列と文字の中の 1 文字 (\n などを読む) */
static int read_char(void)
{
    int c = (unsigned char)*lx.p++;
    if (c != '\\')
        return c;
    c = (unsigned char)*lx.p++;
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'b': return '\b';
    case 'e': return 0x1B;
    case '0': return 0;
    case 'x': {
        int v = 0, d;
        while ((d = hex_val(*lx.p)) >= 0)
            v = v * 16 + d, lx.p++;
        return v & 0xFF;
    }
    case 0:
        fail("unterminated escape");
    default:  return c;                 /* \\ \" \' など */
    }
}

static void next(void)
{
    while (*lx.p == ' ' || *lx.p == '\t')
        lx.p++;
    const char c = *lx.p;
    if (!c) {
        lx.type = T_END;
        return;
    }
    if (is_digit(c)) {
        uint32_t v = 0;
        if (c == '0' && (lx.p[1] == 'x' || lx.p[1] == 'X')) {
            lx.p += 2;
            int d;
            if (hex_val(*lx.p) < 0)
                fail("bad number");
            while ((d = hex_val(*lx.p)) >= 0)
                v = v * 16 + (uint32_t)d, lx.p++;
        } else if (c == '0') {          /* 0 で始まる数は 8 進 (C と同じ) */
            while (*lx.p >= '0' && *lx.p <= '7')
                v = v * 8 + (uint32_t)(*lx.p++ - '0');
        } else {
            while (is_digit(*lx.p))
                v = v * 10 + (uint32_t)(*lx.p++ - '0');
        }
        if (is_alpha(*lx.p) || is_digit(*lx.p))
            fail("bad number");
        lx.type = T_NUM;
        lx.num = (int32_t)v;
        return;
    }
    if (is_alpha(c)) {
        int n = 0;
        while (is_alpha(*lx.p) || is_digit(*lx.p)) {
            if (n < NAME_MAX)
                lx.name[n++] = *lx.p;
            lx.p++;
        }
        lx.name[n] = 0;
        lx.type = T_IDENT;
        return;
    }
    if (c == '"') {
        lx.p++;
        char tmp[LINE_MAX + 1];
        int n = 0;
        while (*lx.p != '"') {
            if (!*lx.p)
                fail("unterminated string");
            tmp[n++] = (char)read_char();
        }
        lx.p++;
        lx.str = pool_alloc(n + 1);
        for (int i = 0; i < n; i++)
            lx.str[i] = tmp[i];
        lx.str[n] = 0;
        lx.type = T_STR;
        return;
    }
    if (c == '\'') {
        lx.p++;
        if (*lx.p == '\'' || !*lx.p)
            fail("bad character constant");
        lx.num = read_char();
        if (*lx.p++ != '\'')
            fail("bad character constant");
        lx.type = T_NUM;
        return;
    }
    static const struct { char s[3]; int op; } two[] = {
        { "<<", OP_SHL }, { ">>", OP_SHR }, { "<=", OP_LE }, { ">=", OP_GE },
        { "==", OP_EQ }, { "!=", OP_NE }, { "&&", OP_LAND }, { "||", OP_LOR },
    };
    lx.type = T_OP;
    for (unsigned i = 0; i < sizeof two / sizeof two[0]; i++) {
        if (c == two[i].s[0] && lx.p[1] == two[i].s[1]) {
            lx.op = two[i].op;
            lx.p += 2;
            return;
        }
    }
    if (!(c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '&' || c == '|' ||
          c == '^' || c == '~' || c == '!' || c == '<' || c == '>' || c == '=' || c == '(' ||
          c == ')' || c == ','))
        fail("unexpected character");
    lx.op = c;
    lx.p++;
}

static int is_op(int op)
{
    return lx.type == T_OP && lx.op == op;
}

static void expect(int op, const char *msg)
{
    if (!is_op(op))
        fail(msg);
    next();
}

/* ---- 式 ---- */

enum { RV, LV_MEM, LV_VAR, LV_NEW };    /* 値 / メモリ (32 ビット) / シェルの変数 / まだない変数 */

struct val {
    int kind;
    int32_t v;                          /* RV の値 */
    uint32_t addr;                      /* LV_MEM のアドレス */
    int var;                            /* LV_VAR の番号 */
    char name[NAME_MAX + 1];            /* LV_NEW の名前 */
};

static int created;                     /* この行で作った変数の番号 (-1 = なし) */

static struct val rv(int32_t v)
{
    struct val r;
    r.kind = RV;
    r.v = v;
    return r;
}

static int32_t get(const struct val *x)
{
    switch (x->kind) {
    case LV_MEM:
        return *(volatile int32_t *)x->addr;
    case LV_VAR:
        return vars[x->var].value;
    case LV_NEW:
        sprintf(err_buf, "undefined symbol: %s", x->name);
        fail(err_buf);
    default:
        return x->v;
    }
}

static void set(const struct val *x, int32_t v)
{
    switch (x->kind) {
    case LV_MEM:
        *(volatile int32_t *)x->addr = v;
        break;
    case LV_VAR:
        vars[x->var].value = v;
        break;
    case LV_NEW:
        if (n_vars == VARS_N)
            fail("too many variables");
        str_copy(vars[n_vars].name, x->name, NAME_MAX);
        vars[n_vars].value = v;
        created = n_vars++;
        break;
    default:
        fail("not assignable");
    }
}

/* 名前を値にする */
static struct val lookup(const char *name)
{
    struct val r = rv(0);
    for (int i = 0; i < n_vars; i++) {
        if (str_eq(vars[i].name, name)) {
            r.kind = LV_VAR;
            r.var = i;
            return r;
        }
    }
    const struct symtab_entry *s = sym_find(name);
    if (s && symtab_is_func(s->type))
        return rv((int32_t)s->addr);
    if (s) {
        r.kind = LV_MEM;
        r.addr = s->addr;
        return r;
    }
    r.kind = LV_NEW;
    str_copy(r.name, name, NAME_MAX);
    return r;
}

typedef int32_t (*fn_t)(int32_t, int32_t, int32_t, int32_t, int32_t,
                        int32_t, int32_t, int32_t, int32_t, int32_t);

static int32_t call(uint32_t addr, const int32_t *a)
{
    if (!addr)
        fail("call to address 0");
    console_flush();
    return ((fn_t)addr)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
}

static struct val expr(void);

/* 引数の並び (かっこの中、または関数名の後ろ) を読む */
static void args(int32_t *a, int end_op)
{
    for (int i = 0; i < MAX_ARGS; i++)
        a[i] = 0;
    if (end_op ? is_op(end_op) : lx.type == T_END)
        return;
    for (int n = 0;; n++) {
        if (n == MAX_ARGS)
            fail("too many arguments");
        struct val v = expr();
        a[n] = get(&v);
        if (!is_op(','))
            break;
        next();
    }
}

static struct val primary(void)
{
    struct val r;
    switch (lx.type) {
    case T_NUM:
        r = rv(lx.num);
        next();
        return r;
    case T_STR:
        r = rv((int32_t)(uintptr_t)lx.str);
        next();
        return r;
    case T_IDENT:
        r = lookup(lx.name);
        next();
        return r;
    case T_OP:
        if (lx.op == '(') {
            next();
            r = expr();
            expect(')', "missing ')'");
            return r;
        }
        break;
    }
    fail(lx.type == T_END ? "unexpected end of line" : "syntax error");
}

static struct val postfix(void)
{
    struct val r = primary();
    while (is_op('(')) {                /* 呼び出し */
        next();
        int32_t a[MAX_ARGS];
        const uint32_t addr = (uint32_t)get(&r);
        args(a, ')');
        expect(')', "missing ')'");
        r = rv(call(addr, a));
    }
    return r;
}

static struct val unary(void)
{
    if (lx.type == T_OP) {
        const int op = lx.op;
        if (op == '-' || op == '~' || op == '!' || op == '*' || op == '&' || op == '+') {
            next();
            struct val x = unary();
            switch (op) {
            case '-': return rv(-get(&x));
            case '~': return rv(~get(&x));
            case '!': return rv(!get(&x));
            case '+': return rv(get(&x));
            case '*': {
                struct val r = rv(0);
                r.kind = LV_MEM;
                r.addr = (uint32_t)get(&x);
                return r;
            }
            default:                    /* '&' */
                if (x.kind == LV_MEM)
                    return rv((int32_t)x.addr);
                if (x.kind == LV_VAR)
                    return rv((int32_t)(uintptr_t)&vars[x.var].value);
                fail("cannot take address");
            }
        }
    }
    return postfix();
}

/* 2 項演算子の優先順位 (大きいほど強い)。0 = 2 項演算子ではない */
static int prec(void)
{
    if (lx.type != T_OP)
        return 0;
    switch (lx.op) {
    case '*': case '/': case '%': return 10;
    case '+': case '-':           return 9;
    case OP_SHL: case OP_SHR:     return 8;
    case '<': case '>': case OP_LE: case OP_GE: return 7;
    case OP_EQ: case OP_NE:       return 6;
    case '&':                     return 5;
    case '^':                     return 4;
    case '|':                     return 3;
    case OP_LAND:                 return 2;
    case OP_LOR:                  return 1;
    default:                      return 0;
    }
}

static struct val binary(int min_prec)
{
    struct val l = unary();
    for (int p; (p = prec()) >= min_prec && p > 0; ) {
        const int op = lx.op;
        next();
        struct val rr = binary(p + 1);
        const int32_t a = get(&l), b = get(&rr);
        int32_t v;
        switch (op) {
        case '*': v = a * b; break;
        case '/': case '%':
            if (b == 0)
                fail("division by zero");
            v = op == '/' ? a / b : a % b;
            break;
        case '+': v = a + b; break;
        case '-': v = a - b; break;
        case OP_SHL: v = (int32_t)((uint32_t)a << (b & 31)); break;
        case OP_SHR: v = a >> (b & 31); break;
        case '<': v = a < b; break;
        case '>': v = a > b; break;
        case OP_LE: v = a <= b; break;
        case OP_GE: v = a >= b; break;
        case OP_EQ: v = a == b; break;
        case OP_NE: v = a != b; break;
        case '&': v = a & b; break;
        case '^': v = a ^ b; break;
        case '|': v = a | b; break;
        case OP_LAND: v = a && b; break;
        default:  v = a || b; break;    /* OP_LOR */
        }
        l = rv(v);
    }
    return l;
}

static struct val expr(void)
{
    struct val l = binary(1);
    if (is_op('=')) {                   /* 代入 (右から結合) */
        next();
        struct val r = expr();
        const int32_t v = get(&r);
        set(&l, v);
        return rv(v);
    }
    return l;
}

/* ---- 1 行の実行 ---- */

static void print_value(int32_t v)
{
    if (console_col() != 0)             /* 関数が改行せずに終わったとき */
        console_putc('\n');
    printf("value = %d = 0x%x", (int)v, (unsigned)v);
    if (v >= 0x20 && v <= 0x7E)
        printf(" = '%c'", (int)v);
    console_putc('\n');
}

void shell_exec(const char *line)
{
    static struct trap_info ti;         /* static: 例外で戻ってきたときに値が残っているように */
    created = -1;
    lx.p = line;
    if (__builtin_setjmp(err_jmp)) {    /* fail() で戻ってきた */
        trap_release();
        printf("error: %s\n", err_msg);
        return;
    }
    const int vec = trap_catch(&ti);
    if (vec) {
        const struct symtab_entry *s = shell_sym_near(ti.pc);
        printf("\nexception: %s (vector %d) at pc 0x%08x", trap_name(vec), vec, (unsigned)ti.pc);
        if (s)
            printf(" (%s+0x%x)", s->name, (unsigned)(ti.pc - s->addr));
        if (ti.has_addr)
            printf(", address 0x%08x", (unsigned)ti.addr);
        console_putc('\n');
        return;
    }
    next();
    if (lx.type == T_END) {
        trap_release();
        return;
    }
    int32_t value;
    /* 先頭が関数の名前で、後ろが '(' でも '=' でもなければ、かっこなしの呼び出し */
    const struct symtab_entry *fs = lx.type == T_IDENT ? sym_find(lx.name) : 0;
    int is_var = 0;
    for (int i = 0; fs && i < n_vars; i++)
        if (str_eq(vars[i].name, lx.name))
            is_var = 1;
    if (fs && symtab_is_func(fs->type) && !is_var) {
        const char *save = lx.p;
        next();
        if (is_op('(') || is_op('=')) {
            lx.p = line;                /* ふつうの式として読み直す */
            next();
            struct val v = expr();
            value = get(&v);
        } else {
            lx.p = save;
            next();
            int32_t a[MAX_ARGS];
            args(a, 0);
            value = call(fs->addr, a);
        }
    } else {
        struct val v = expr();
        value = get(&v);
    }
    if (lx.type != T_END)
        fail("syntax error");
    trap_release();
    if (created >= 0)
        printf("new variable \"%s\"\n", vars[created].name);
    print_value(value);
}

void shell_run(void)
{
    static char line[LINE_MAX + 1];
    for (;;) {
        if (shell_readline(PROMPT, line, LINE_MAX, 1) > 0)
            shell_exec(line);
    }
}
