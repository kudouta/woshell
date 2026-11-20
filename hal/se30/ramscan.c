/**
 * @file ramscan.c
 * @brief 物理 RAM がどこにあるかを調べる (platform_ram_scan, platform.h。調査用)
 *
 * 24 ビットのモードでは、ROM の変換テーブルが RAM を 8MB の窓 (論理アドレス $000000-$7FFFFF) にしか割り当てないので、
 * それより先の物理 RAM は見えない。そこで CPU のトランスペアレント変換の範囲 (cpu_phys_window) を一時的に使い、
 * 物理アドレスを直接読み書きして調べる。
 *
 * 調べ方 (1MB のブロックごと。各ブロックの先頭から PROBE_OFFSET の 2 語だけを使う):
 *   1. あるか: 元の値を覚えて、決まった模様を書いて読み戻す (間に隣の語へ別の値を書き、読み戻しが
 *      バスに残った値でないようにする)。2 つの模様とも読み戻せたら RAM がある。元の値に戻す
 *   2. ミラー: RAM が、ブロックの数より小さい間隔で繰り返して見えていることがある (同じ RAM が別のアドレスにも見える)。
 *      RAM のあるブロックに番号入りの目印を上のアドレスから順に書き、読み戻す。ほかのブロックの目印が読めたら、
 *      そのブロックのミラー。ROM が見つけた RAM (MemTop まで) のブロックにも目印を書いて比べる。
 *      最後に、書いたのと逆の順で元の値に戻す (ミラーで同じ場所に何度も書いても、元の値に戻る)
 * 調べている間は割り込みを止める。RAM のないアドレスでバスエラーになったときは、例外としてプロンプトに戻る
 * (そのときトランスペアレント変換の範囲が残るが、woshell が使うアドレスの見え方は変わらない。もう一度 ramscan を実行すると解ける)。
 */
#include "platform.h"
#include "cpu.h"
#include "irq.h"
#include "mac.h"
#include "fmt.h"

#define BLOCK          (0x100000u)      /* 1MB */
#define PROBE_OFFSET   (0x100u)
#define RAM_SPACE_END  (0x40000000u)    /* SE/30 の RAM の空間 (物理 $00000000-$3FFFFFFF) */
#define MAX_BLOCKS     (RAM_SPACE_END / BLOCK)
#define PAT1           (0x5A5AA5A5u)
#define PAT2           (0xA5A55A5Au)
#define TAG            (0xC0DE0000u)

static uint8_t present[MAX_BLOCKS];
static int16_t alias_of[MAX_BLOCKS];    /* ミラーの元のブロック (-1 = なし) */
static uint32_t saved[MAX_BLOCKS];
static uint32_t window_top;

static volatile uint32_t *probe_addr(uint32_t blk)
{
    return (volatile uint32_t *)(blk * BLOCK + PROBE_OFFSET);
}

/* ブロックの物理アドレスが見えるようにする (上位 8 ビットが変わったときだけ設定し直す) */
static volatile uint32_t *open_block(uint32_t blk)
{
    volatile uint32_t *p = probe_addr(blk);
    if (((uint32_t)p >> 24) != window_top) {
        cpu_phys_window((uint32_t)p);
        window_top = (uint32_t)p >> 24;
    }
    return p;
}

/* ブロックに RAM があるか */
static int probe(uint32_t blk)
{
    volatile uint32_t *p = open_block(blk);
    const uint32_t s0 = p[0], s1 = p[1];
    p[0] = PAT1;
    p[1] = 0;
    const uint32_t v1 = p[0];
    p[0] = PAT2;
    p[1] = 0xFFFFFFFFu;
    const uint32_t v2 = p[0];
    p[0] = s0;
    p[1] = s1;
    return v1 == PAT1 && v2 == PAT2;
}

/* 範囲 [b, e) を 1 行で出す。ミラーなら、元になっているブロックの範囲 [src_lo, src_hi] も出す */
static void print_run(uint32_t b, uint32_t e, int kind, int src_lo, int src_hi)
{
    printf("  %08x-%08x %5u MB  ", (unsigned)(b * BLOCK), (unsigned)(e * BLOCK - 1), (unsigned)(e - b));
    if (kind == 0)
        printf("no RAM\n");
    else if (kind == 1)
        printf("RAM\n");
    else
        printf("mirror of %08x-%08x\n", (unsigned)(src_lo * BLOCK), (unsigned)((src_hi + 1) * BLOCK - 1));
}

void platform_ram_scan(uint32_t start, uint32_t end)
{
    const uint32_t in_use = MemTop;
    if (start == 0)
        start = in_use;
    start = (start + BLOCK - 1) & ~(BLOCK - 1);
    if (end == 0 || end > RAM_SPACE_END)
        end = RAM_SPACE_END;
    if (start >= end) {
        printf("nothing to scan\n");
        return;
    }
    const uint32_t b0 = start / BLOCK, b1 = end / BLOCK, used = (in_use + BLOCK - 1) / BLOCK;
    printf("RAM found by the ROM (MemTop): %08x (%u KB), usable up to BufPtr %08x\n",
           (unsigned)in_use, (unsigned)(in_use >> 10), (unsigned)BufPtr);
    printf("scanning physical %08x-%08x by 1MB...\n", (unsigned)start, (unsigned)(end - 1));

    const uint16_t sr = irq_disable();
    window_top = 0xFFFFFFFFu;

    /* 1. あるか */
    for (uint32_t b = b0; b < b1; b++) {
        present[b] = (uint8_t)probe(b);
        alias_of[b] = -1;
    }
    /* 2. ミラー: 目印を上のアドレスから順に書く (いちばん低いアドレスの目印が残る) */
    for (uint32_t b = b1; b-- > 0; ) {
        if (b >= b0 ? !present[b] : b >= used)
            continue;
        volatile uint32_t *p = open_block(b);
        saved[b] = *p;
        *p = TAG | b;
    }
    for (uint32_t b = b0; b < b1; b++) {
        if (!present[b])
            continue;
        const uint32_t v = *open_block(b);
        if (v != (TAG | b) && (v & 0xFFFF0000u) == TAG && (v & 0xFFFFu) < b)
            alias_of[b] = (int16_t)(v & 0xFFFFu);
    }
    /* 書いたのと逆の順 (下のアドレスから) で元に戻す */
    for (uint32_t b = 0; b < b1; b++) {
        if (b >= b0 ? !present[b] : b >= used)
            continue;
        *open_block(b) = saved[b];
    }
    cpu_phys_window_off();
    irq_restore(sr);

    /* 3. まとめて出す (同じ種類が続く範囲を 1 行に。ミラーは元の範囲もまとめる) */
    uint32_t total = 0;
    uint32_t run_b = b0;
    int run_kind = -1, src_lo = 0, src_hi = 0;
    for (uint32_t b = b0; b <= b1; b++) {
        int kind = 0;
        if (b < b1) {
            kind = !present[b] ? 0 : alias_of[b] < 0 ? 1 : 2;
            if (kind == 1)
                total++;
        }
        if (b == b1 || kind != run_kind) {
            if (run_kind >= 0)
                print_run(run_b, b, run_kind, src_lo, src_hi);
            run_b = b;
            run_kind = kind;
            src_lo = src_hi = (b < b1 && kind == 2) ? alias_of[b] : 0;
        } else if (kind == 2) {
            if (alias_of[b] < src_lo)
                src_lo = alias_of[b];
            if (alias_of[b] > src_hi)
                src_hi = alias_of[b];
        }
    }
    /* 合計の見積もり: 調べ始めより下は、ROM が見つけた分 (MemTop まで) があるとみなす */
    const uint32_t below = start < in_use ? start : in_use;
    printf("RAM found above %08x: %u MB (not counting mirrors). total with RAM below: about %u MB\n",
           (unsigned)start, (unsigned)total, (unsigned)(total + (below >> 20)));
}
