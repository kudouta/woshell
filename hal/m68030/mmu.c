/**
 * @file mmu.c
 * @brief 68030 の MMU (PMMU) とキャッシュの状態を表示する (cpu.h。調査用)
 *
 * レジスタ (TC, CRP, SRP, TT0, TT1, CACR) を pmove / movec で読み、変換の表をたどって、
 * 論理番地から物理番地への対応を、続いている範囲ごとにまとめて表示する。表は読むだけで書き換えない。
 *
 * 68030 の変換 (MC68030 User's Manual 9 章):
 *   TC   E(31) 有効, SRE(25) スーパーバイザーは SRP を使う, FCL(24) 最初の段を機能コードで引く,
 *        PS(23-20) ページの大きさ 2^PS, IS(19-16) 無視する上位のビット数, TIA-TID(15-0) 各段の索引のビット数
 *   CRP / SRP (64 ビット) 上位: L/U(31) LIMIT(30-16) DT(1-0)、下位: 表の番地
 *   記述子の DT: 0 = 無効, 1 = ページ (途中の段なら、その段で打ち切った大きなかたまり), 2 = 短い形 (4 バイト) の表,
 *        3 = 長い形 (8 バイト) の表。最後の段で 2 / 3 なら、ページの記述子を指す間接の記述子
 *   短い形のページの記述子: 番地(31-8) CI(6) M(4) U(3) WP(2) DT(1-0)。長い形は 1 語目に S(8) CI WP DT、2 語目に番地
 *   TT0 / TT1 (素通し): 論理番地の上位 8 ビット BASE(31-24) と MASK(23-16) が合えば変換しない。E(15) CI(10)
 * 表の中の番地は物理番地。ここでは論理番地と同じとみなして読む (表の置き場所が 1 対 1 に変換されていない
 * 場合は、正しく読めない。そのときは表示の最後に知らせる)。
 * 表をたどるのは、スーパーバイザーのデータ (機能コード 5) の場合。
 */
#include "cpu.h"
#include "fmt.h"

#define FC_SUPER_DATA (5)

/* ---- レジスタ ---- */

static uint32_t read_tc(void)
{
    uint32_t v;
    __asm__ volatile("pmove %%tc,%0" : "=m"(v));
    return v;
}

static uint64_t read_crp(void)
{
    uint64_t v;
    __asm__ volatile("pmove %%crp,%0" : "=m"(v));
    return v;
}

static uint64_t read_srp(void)
{
    uint64_t v;
    __asm__ volatile("pmove %%srp,%0" : "=m"(v));
    return v;
}

static uint32_t read_tt0(void)
{
    uint32_t v;
    __asm__ volatile("pmove %%tt0,%0" : "=m"(v));
    return v;
}

static uint32_t read_tt1(void)
{
    uint32_t v;
    __asm__ volatile("pmove %%tt1,%0" : "=m"(v));
    return v;
}

static void write_tt1(uint32_t v)
{
    __asm__ volatile("pmove %0,%%tt1" :: "m"(v) : "memory");
}

static uint32_t read_cacr(void)
{
    uint32_t v;
    __asm__ volatile("movec %%cacr,%0" : "=d"(v));
    return v;
}

static uint32_t rd32(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* ---- TC の読み解き ---- */

struct layout {
    int enabled, sre, fcl, ps, is;
    int ti[4];                          /* 各段の索引のビット数 */
    int levels;                         /* 段の数 (0 のところまで) */
};

static void decode_tc(uint32_t tc, struct layout *l)
{
    l->enabled = (tc >> 31) & 1;
    l->sre = (tc >> 25) & 1;
    l->fcl = (tc >> 24) & 1;
    l->ps = (tc >> 20) & 15;
    l->is = (tc >> 16) & 15;
    l->levels = 0;
    for (int i = 0; i < 4; i++) {
        l->ti[i] = (tc >> (12 - 4 * i)) & 15;
        if (l->ti[i] && l->levels == i)
            l->levels = i + 1;
    }
}

/* TT0 / TT1 が番地 addr (スーパーバイザーのデータ) に当てはまるか */
static int tt_match(uint32_t tt, uint32_t addr)
{
    if (!(tt & 0x8000))
        return 0;
    const uint32_t base = tt >> 24, mask = (tt >> 16) & 0xFF;
    const uint32_t fcbase = (tt >> 4) & 7, fcmask = tt & 7;
    return ((addr >> 24) & ~mask & 0xFF) == (base & ~mask) &&
           (FC_SUPER_DATA & ~fcmask & 7) == (fcbase & ~fcmask);
}

static void print_tt(const char *name, uint32_t tt)
{
    printf("%s  %08x", name, (unsigned)tt);
    if (!(tt & 0x8000)) {
        printf("  (off)\n");
        return;
    }
    const unsigned base = tt >> 24, mask = (tt >> 16) & 0xFF;
    printf("  on: %08x-%08x%s, FC base %u mask %u\n", base << 24, ((base | mask) << 24) | 0xFFFFFF,
           (tt & 0x400) ? " cache-inhibit" : "", (unsigned)((tt >> 4) & 7), (unsigned)(tt & 7));
}

/* ---- 対応の範囲をまとめて出す ---- */

#define F_CI (1)                        /* キャッシュしない */
#define F_WP (2)                        /* 書き込み禁止 */
#define F_S  (4)                        /* スーパーバイザーだけ (長い形の記述子) */

static struct {
    int active;
    uint32_t la, pa;
    uint64_t len;
    int flags;
} run;
static int ranges;
static uint32_t table_lo = 0xFFFFFFFFu, table_hi;   /* 読んだ表の番地の範囲 */

static void flush_run(void)
{
    if (!run.active)
        return;
    const uint32_t last = (uint32_t)(run.la + run.len - 1);
    printf("  %08x-%08x -> %08x%s%s%s%s\n", (unsigned)run.la, (unsigned)last, (unsigned)run.pa,
           run.la == run.pa ? " (same)" : "",
           (run.flags & F_CI) ? " CI" : "", (run.flags & F_WP) ? " WP" : "", (run.flags & F_S) ? " S" : "");
    run.active = 0;
    ranges++;
}

static void emit(uint32_t la, uint32_t pa, uint64_t len, int flags)
{
    if (run.active && run.flags == flags && run.la + run.len == la && run.pa + run.len == pa) {
        run.len += len;
        return;
    }
    flush_run();
    run.active = 1;
    run.la = la;
    run.pa = pa;
    run.len = len;
    run.flags = flags;
}

/* 記述子を読む。dt と番地と印を返す */
static void read_desc(uint32_t at, int longfmt, int *dt, uint32_t *addr, int *flags)
{
    if (at < table_lo)
        table_lo = at;
    if (at > table_hi)
        table_hi = at;
    if (longfmt) {
        const uint32_t d0 = rd32(at), d1 = rd32(at + 4);
        *dt = d0 & 3;
        *addr = d1;
        *flags = ((d0 & 0x40) ? F_CI : 0) | ((d0 & 4) ? F_WP : 0) | ((d0 & 0x100) ? F_S : 0);
    } else {
        const uint32_t d = rd32(at);
        *dt = d & 3;
        *addr = d & ~0xFu;
        *flags = ((d & 0x40) ? F_CI : 0) | ((d & 4) ? F_WP : 0);
    }
}

/* 段 level の表 (番地 table, 形 longfmt) をたどる。la は、この表が受け持つ論理番地の始まり */
static void walk(const struct layout *l, int level, uint32_t table, int longfmt, uint32_t la, int used, int flags_in)
{
    const int n = l->ti[level];
    const int remaining = 32 - used - n;    /* この段の 1 項目が受け持つビット数 */
    const uint64_t block = (uint64_t)1 << remaining;
    for (uint32_t i = 0; i < (1u << n); i++) {
        const uint32_t ela = la + (uint32_t)(i * block);
        int dt, flags;
        uint32_t addr;
        read_desc(table + i * (longfmt ? 8 : 4), longfmt, &dt, &addr, &flags);
        flags |= flags_in & (F_WP | F_S);
        if (dt == 0) {
            flush_run();
            continue;
        }
        if (dt != 1 && level == l->levels - 1) {        /* 間接の記述子: ページの記述子を指す */
            int dt2, flags2;
            uint32_t addr2;
            read_desc(addr & ~3u, dt == 3, &dt2, &addr2, &flags2);
            if (dt2 != 1) {
                flush_run();
                continue;
            }
            dt = 1;
            addr = addr2;
            flags |= flags2;
        }
        if (dt == 1) {
            const uint32_t base = (uint32_t)(addr & ~(uint32_t)(block - 1)) & ~0xFFu;
            emit(ela, base, block, flags);
            continue;
        }
        walk(l, level + 1, addr & ~0xFu, dt == 3, ela, used + n, flags & (F_WP | F_S));
    }
}

void cpu_mmu_report(void)
{
    const uint32_t tc = read_tc(), tt0 = read_tt0(), tt1 = read_tt1(), cacr = read_cacr();
    const uint64_t crp = read_crp(), srp = read_srp();
    struct layout l;
    decode_tc(tc, &l);

    printf("TC   %08x  %s", (unsigned)tc, l.enabled ? "enabled" : "disabled");
    printf(", page %u bytes, IS %d, TIA-TID %d %d %d %d%s%s\n", 1u << l.ps, l.is,
           l.ti[0], l.ti[1], l.ti[2], l.ti[3], l.sre ? ", SRE" : "", l.fcl ? ", FCL" : "");
    printf("CRP  %08x %08x  DT %u, limit %u%s\n", (unsigned)(crp >> 32), (unsigned)crp,
           (unsigned)(crp >> 32) & 3, (unsigned)(crp >> 48) & 0x7FFF, (crp >> 63) ? " (lower)" : "");
    printf("SRP  %08x %08x\n", (unsigned)(srp >> 32), (unsigned)srp);
    print_tt("TT0", tt0);
    print_tt("TT1", tt1);
    printf("CACR %08x  data cache %s, instruction cache %s\n", (unsigned)cacr,
           (cacr & 0x100) ? "on" : "off", (cacr & 0x1) ? "on" : "off");
    if (!l.enabled)
        return;

    /* スーパーバイザーのデータとして表をたどる */
    const uint64_t root = l.sre ? srp : crp;
    int dt = (int)(root >> 32) & 3;
    uint32_t table = (uint32_t)root & ~0xFu;
    const int space = 32 - l.is;
    printf("map (supervisor data, root %s, logical space 2^%d):\n", l.sre ? "SRP" : "CRP", space);
    run.active = 0;
    ranges = 0;
    table_lo = 0xFFFFFFFFu;
    table_hi = 0;
    if (dt == 1) {                      /* 根が直接ページ: 全部が 1 つのかたまり */
        emit(0, (uint32_t)root & ~0xFFu, (uint64_t)1 << space, 0);
    } else if (dt != 0) {
        int longfmt = dt == 3;
        if (l.fcl) {                    /* 最初の段は機能コードで引く */
            int flags;
            uint32_t addr;
            read_desc(table + FC_SUPER_DATA * (longfmt ? 8 : 4), longfmt, &dt, &addr, &flags);
            table = addr & ~0xFu;
            longfmt = dt == 3;
        }
        if (dt >= 2)
            walk(&l, 0, table, longfmt, 0, l.is, 0);
    }
    flush_run();
    printf("%d ranges. tables read at %08x-%08x\n", ranges, (unsigned)table_lo, (unsigned)table_hi + 7);
    if (tt0 & 0x8000 || tt1 & 0x8000)
        printf("note: TT0/TT1 ranges bypass the tables above\n");
}

/* ---- 物理番地の窓 (TT1) ---- */

static uint32_t saved_tt1;
static int window_on;

void cpu_phys_window(uint32_t paddr)
{
    if (!window_on) {
        saved_tt1 = read_tt1();
        window_on = 1;
    }
    /* BASE = paddr の上位 8 ビット, MASK = 0, E, CI, RWM (読み書きの両方), FC MASK = 7 (どの機能コードでも) */
    write_tt1((paddr & 0xFF000000u) | 0x8000u | 0x400u | 0x100u | 0x7u);
}

void cpu_phys_window_off(void)
{
    if (!window_on)
        return;
    write_tt1(saved_tt1);
    window_on = 0;
}

void cpu_mmu_report_addr(uint32_t addr)
{
    const uint32_t tc = read_tc(), tt0 = read_tt0(), tt1 = read_tt1();
    const uint64_t crp = read_crp(), srp = read_srp();
    struct layout l;
    decode_tc(tc, &l);
    printf("%08x: ", (unsigned)addr);
    if (!l.enabled) {
        printf("MMU disabled (physical = logical)\n");
        return;
    }
    if (tt_match(tt0, addr) || tt_match(tt1, addr)) {
        printf("transparent (%s)\n", tt_match(tt0, addr) ? "TT0" : "TT1");
        return;
    }
    const uint64_t root = l.sre ? srp : crp;
    int dt = (int)(root >> 32) & 3, longfmt = dt == 3, flags = 0, used = l.is;
    uint32_t a = (uint32_t)root & ~0xFu;
    if (dt == 1) {
        printf("-> %08x (root page)\n", (unsigned)((a & ~0xFFu) + (addr << l.is >> l.is)));
        return;
    }
    if (l.fcl && dt >= 2) {
        int f;
        read_desc(a + FC_SUPER_DATA * (longfmt ? 8 : 4), longfmt, &dt, &a, &f);
        a &= ~0xFu;
        longfmt = dt == 3;
    }
    for (int level = 0; level < l.levels && dt >= 2; level++) {
        const int n = l.ti[level];
        const uint32_t index = (addr << used) >> (32 - n);
        const uint32_t at = a + index * (longfmt ? 8 : 4);
        int f;
        read_desc(at, longfmt, &dt, &a, &f);
        flags |= f;
        printf("[%c%u @%08x DT%d] ", 'A' + level, (unsigned)index, (unsigned)at, dt);
        used += n;
        if (dt >= 2 && level == l.levels - 1) {     /* 間接の記述子 */
            read_desc(a & ~3u, dt == 3, &dt, &a, &f);
            flags |= f;
        }
        if (dt == 1) {
            const uint32_t offset = used >= 32 ? 0 : (addr << used) >> used;
            printf("-> %08x%s%s\n", (unsigned)((a & ~((1u << (32 - used)) - 1) & ~0xFFu) + offset),
                   (flags & F_CI) ? " CI" : "", (flags & F_WP) ? " WP" : "");
            return;
        }
        a &= ~0xFu;
        longfmt = dt == 3;
    }
    printf("invalid\n");
}
