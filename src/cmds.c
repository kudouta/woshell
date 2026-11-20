/**
 * @file cmds.c
 * @brief シェルの組み込みの関数
 *
 * どれもふつうの C の関数で、シンボルテーブルから名前で呼ばれる (引数はすべて 32 ビットの整数)。
 * 名前が短いものが多いのは、シェルで打ちやすくするため。
 */
#include <stdint.h>
#include "console.h"
#include "fmt.h"
#include "shell.h"
#include "cmds.h"
#include "symtab.h"
#include "keyboard.h"
#include "video.h"
#include "timer.h"
#include "platform.h"
#include "rtc.h"
#include "beep.h"
#include "cpu.h"
#include "serial.h"

extern char _start[], __bss_end[], __stack_top[];     /* stage2.ld */

static const struct { const char *usage, *text; } help_items[] = {
    { "help",                     "this list" },
    { "d [addr[,n[,width]]]",     "display memory (n units, width 1/2/4)" },
    { "m addr[,width]",           "modify memory (Enter = next, '.' = quit)" },
    { "printf \"fmt\", ...",      "formatted print (%d %x %s %c ...)" },
    { "h",                        "command history" },
    { "vars",                     "shell variables" },
    { "mem",                      "memory map" },
    { "malloc n / free p",        "allocate memory (free does nothing)" },
    { "ticks",                    "vsync count since boot" },
    { "irqs",                     "interrupt counts per level" },
    { "delay n",                  "wait n vsyncs" },
    { "date",                     "show the clock (returns seconds since 1904)" },
    { "setdate y,mo,d[,h,mi,s]",  "set the clock" },
    { "pram [addr[,n]]",          "display parameter RAM (256 bytes)" },
    { "pramw addr,v",             "write one byte of parameter RAM" },
    { "serial",                   "serial port, speed and counts" },
    { "port 'a' / port 'b'",      "use the modem (a) or printer (b) port" },
    { "baud n",                   "set serial speed (300-57600, 115200, 230400)" },
    { "serout on",                "copy screen output to serial (1) or not (0)" },
    { "cls",                      "clear screen" },
    { "beep [hz[,ms]]",           "sound a tone (default 880 Hz, 120 ms)" },
    { "mmu [addr]",               "show MMU and cache state, or how addr is translated" },
    { "ramscan [start[,end]]",    "find physical RAM above MemTop (1MB steps)" },
    { "scrbench [lines]",         "measure screen read/write/move and scrolling speed" },
    { "wo",                       "a fish swims across the line" },
    { "version",                  "show version" },
    { "reboot",                   "restart the machine (ROM)" },
};

int cmd_help(void)
{
    printf("Expressions are C-like:  x = 3 + 4    *0x80000    &x    'A'\n");
    printf("Call any function by name:  func a, b   or   func(a, b)\n");
    printf("Keys: arrows / ^A ^E ^K ^U edit, Up/Down history, ^C cancel,\n");
    printf("      ^L clear. Interrupt button aborts a running command.\n\n");
    for (unsigned i = 0; i < sizeof help_items / sizeof help_items[0]; i++)
        printf("  %-24s %s\n", help_items[i].usage, help_items[i].text);
    return 0;
}

int cmd_version(void)
{
    printf("woshell for " MACHINE_NAME " (bare metal) - built " __DATE__ " " __TIME__ "\n");
    return 0;
}

int cmd_cls(void)
{
    console_clear();
    return 0;
}

int cmd_beep(int hz, int ms)
{
    beep_play(hz > 0 ? hz : 880, ms > 0 ? ms : 120);
    return 0;
}

int cmd_ramscan(uint32_t start, uint32_t end)
{
    platform_ram_scan(start, end);
    return 0;
}

int cmd_mmu(uint32_t addr)
{
    if (addr)
        cpu_mmu_report_addr(addr);
    else
        cpu_mmu_report();
    return 0;
}

int cmd_wo(void)
{
    static const char fish[] = "<o))))><";
    const int len = (int)sizeof fish - 1;
    const int cols = console_cols();

    /* 魚の頭の桁 x を右端から左へ 1 桁ずつ動かし、左端の外へ出たら終わる。
       毎回、行を消してから桁を指定して描くので、シリアルに送るのも数バイトで済む */
    for (int x = cols - 1; x > -len; x--) {
        const int skip = x < 0 ? -x : 0;                /* 左端の外に出た部分 */
        printf("\033[2K\033[%dG", (x < 0 ? 0 : x) + 1);
        for (int i = skip; i < len && x + i < cols; i++)
            console_putc(fish[i]);
        console_flush();
        video_wait_vsync();                             /* 2 垂直同期で 1 桁 (約 30 桁/秒) */
        video_wait_vsync();
    }
    console_puts("\033[2K\r");
    return 0;
}

int cmd_h(void)
{
    shell_history();
    return 0;
}

int cmd_vars(void)
{
    shell_vars();
    return 0;
}

uint32_t cmd_ticks(void)
{
    return video_vsync_count();
}

int cmd_irqs(void)
{
    printf("level  count       unhandled   last cause  source\n");
    for (int l = 0; l < PLATFORM_IRQ_LEVELS; l++) {
        const uint32_t u = platform_irq_unhandled(l);
        printf("  %d    %-10u  %-10u  ", l, (unsigned)platform_irq_count(l), (unsigned)u);
        if (u)
            printf("0x%02x        ", (unsigned)platform_irq_detail(l));
        else
            printf("-           ");
        printf("%s\n", platform_irq_name(l));
    }
    uint32_t presses, last_n, last_us;
    platform_nmi_stats(&presses, &last_n, &last_us);
    printf("interrupt button: %u presses; last press: %u NMIs over %u us\n",
           (unsigned)presses, (unsigned)last_n, (unsigned)last_us);
    return 0;
}

int cmd_delay(int n)
{
    while (n-- > 0)
        video_wait_vsync();
    return 0;
}

int cmd_reboot(void)
{
    console_puts("rebooting...\n");
    console_flush();
    platform_reboot();
}

/* ---- 時計 ----
 * 時計は 1904 年 1 月 1 日 (金曜) 0 時からの秒数を数えている。1904 年はうるう年 */

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int month_days(int y, int m)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return days[m - 1] + (m == 2 && is_leap(y));
}

uint32_t cmd_date(void)
{
    const uint32_t secs = rtc_get();
    uint32_t days = secs / 86400u, rest = secs % 86400u;
    static const char wday[7][4] = { "Fri", "Sat", "Sun", "Mon", "Tue", "Wed", "Thu" };
    const char *wd = wday[days % 7];
    int y = 1904, m = 1;
    while (days >= (uint32_t)(is_leap(y) ? 366 : 365))
        days -= is_leap(y) ? 366 : 365, y++;
    while (days >= (uint32_t)month_days(y, m))
        days -= (uint32_t)month_days(y, m), m++;
    printf("%04d-%02d-%02d %s %02u:%02u:%02u\n", y, m, (int)days + 1, wd,
           (unsigned)(rest / 3600), (unsigned)(rest / 60 % 60), (unsigned)(rest % 60));
    return secs;
}

int cmd_setdate(int y, int mo, int d, int h, int mi, int s)
{
    if (y < 1904 || y > 2039 || mo < 1 || mo > 12 || d < 1 || d > month_days(y, mo) ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
        printf("usage: setdate year, month, day, hour, minute, second  (1904-2039)\n");
        return -1;
    }
    uint32_t days = 0;
    for (int i = 1904; i < y; i++)
        days += is_leap(i) ? 366 : 365;
    for (int i = 1; i < mo; i++)
        days += (uint32_t)month_days(y, i);
    days += (uint32_t)(d - 1);
    if (rtc_set(days * 86400u + (uint32_t)(h * 3600 + mi * 60 + s)) < 0) {
        printf("the clock did not keep the new time\n");
        return -1;
    }
    cmd_date();
    return 0;
}

int cmd_pram(int addr, int n)
{
    if (addr < 0 || addr > 255)
        addr = 0;
    if (n <= 0 || addr + n > 256)
        n = 256 - addr;
    for (int line = addr & ~15; line < addr + n; line += 16) {
        printf("%02x: ", line);
        for (int i = line; i < line + 16; i++) {
            if (i >= addr && i < addr + n)
                printf(" %02x", rtc_pram_read(i));
            else
                printf("   ");
        }
        printf("\n");
    }
    return 0;
}

int cmd_pramw(int addr, int v)
{
    if (addr < 0 || addr > 255 || v < 0 || v > 255) {
        printf("usage: pramw addr (0-255), value (0-255)\n");
        return -1;
    }
    rtc_pram_write(addr, (uint8_t)v);
    return rtc_pram_read(addr);
}

/* ---- シリアル ---- */

static const char *port_name(int port)
{
    return port == SERIAL_MODEM ? "modem port (SCC channel A)" :
           port == SERIAL_PRINTER ? "printer port (SCC channel B)" : "none";
}

int cmd_serial(void)
{
    struct serial_stats st;
    serial_get_stats(&st);
    printf("%s: %u bps 8N1\n", port_name(serial_port()), (unsigned)serial_baud());
    printf("received %u bytes, sent %u bytes\n", (unsigned)st.rx, (unsigned)st.tx);
    printf("errors: buffer full %u, SCC overrun %u, framing %u\n",
           (unsigned)st.rx_overflow, (unsigned)st.rx_overrun, (unsigned)st.framing);
    return (int)serial_baud();
}

/* ポートを変える (速さはそのまま)。0 / 'a' / 'm' = モデムポート, 1 / 'b' / 'p' = プリンタポート */
int cmd_port(int p)
{
    int n;
    if (p == 0 || p == 'a' || p == 'A' || p == 'm' || p == 'M')
        n = SERIAL_MODEM;
    else if (p == 1 || p == 'b' || p == 'B' || p == 'p' || p == 'P')
        n = SERIAL_PRINTER;
    else {
        printf("usage: port 'a'  (modem port)  or  port 'b'  (printer port)\n");
        return -1;
    }
    serial_drain();                     /* 送りかけのものを今のポートで送り終える */
    serial_init(n, serial_baud() ? serial_baud() : 9600);
    printf("serial: %s, %u bps\n", port_name(serial_port()), (unsigned)serial_baud());
    return n;
}

int cmd_baud(int n)
{
    serial_drain();                     /* 送りかけのものを今の速さで送り終える */
    const int p = serial_port() < 0 ? SERIAL_MODEM : serial_port();
    if (serial_init(p, (uint32_t)n) < 0) {
        printf("usage: baud n  (300, 600, ... 57600 (n divides 115200), 115200, 230400)\n");
        return -1;
    }
    printf("serial: %s, %u bps\n", port_name(serial_port()), (unsigned)serial_baud());
    return n;
}

int cmd_serout(int on)
{
    console_serial(on);
    return on;
}

/* ---- 画面の速さ ---- */

static uint32_t to_us(uint32_t ticks)
{
    return (uint32_t)((uint64_t)ticks * 1000000u / TIMER_HZ);
}

/* 画面全体の読み出し・書き込み・上へずらす時間と、文字を流す速さを測る */
int cmd_scrbench(int lines)
{
    if (lines <= 0)
        lines = 200;
    console_flush();
    uint32_t t0 = timer_now();
    volatile uint32_t sum = video_read_rows(0, VIDEO_H);
    uint32_t t_read = timer_now() - t0;
    (void)sum;
    t0 = timer_now();
    video_move_rows(0, 8, VIDEO_H - 8);
    uint32_t t_move = timer_now() - t0;
    t0 = timer_now();
    video_fill(0, VIDEO_H, 0);
    uint32_t t_fill = timer_now() - t0;
    console_redraw();

    t0 = timer_now();
    for (int i = 0; i < lines; i++)
        printf("scroll test line %4d  0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", i);
    console_flush();
    uint32_t t_scroll = timer_now() - t0;

    printf("screen read  (%d rows)   %6u us\n", VIDEO_H, (unsigned)to_us(t_read));
    printf("screen write (%d rows)   %6u us\n", VIDEO_H, (unsigned)to_us(t_fill));
    printf("screen move  (%d rows)   %6u us\n", VIDEO_H - 8, (unsigned)to_us(t_move));
    printf("print %d lines          %6u us (%u lines/s)\n", lines, (unsigned)to_us(t_scroll),
           (unsigned)((uint64_t)lines * TIMER_HZ / (t_scroll ? t_scroll : 1)));
    return 0;
}

/* ---- メモリの表示と書き換え ---- */

static uint32_t d_next = (uint32_t)0x80000;
static int d_width = 2;

int cmd_d(uint32_t addr, int n, int width)
{
    if (addr)
        d_next = addr;
    if (width == 1 || width == 2 || width == 4)
        d_width = width;
    if (n <= 0)
        n = 128 / d_width;
    const uint32_t start = d_next;
    uint32_t end = start + (uint32_t)n * (uint32_t)d_width;
    for (uint32_t line = start & ~15u; line < end; line += 16) {
        printf("%08x:  ", (unsigned)line);
        char ascii[17];
        for (int i = 0; i < 16; i += d_width) {
            const uint32_t a = line + (uint32_t)i;
            const int show = a >= start && a < end;
            uint32_t v = 0;
            if (show) {
                if (d_width == 1) v = *(volatile uint8_t *)a;
                else if (d_width == 2) v = *(volatile uint16_t *)a;
                else v = *(volatile uint32_t *)a;
            }
            for (int k = 0; k < d_width; k++) {
                const int b = (int)(v >> (8 * (d_width - 1 - k))) & 0xFF;
                ascii[i + k] = show ? (b >= 0x20 && b <= 0x7E ? (char)b : '.') : ' ';
            }
            if (show)
                printf("%0*x ", d_width * 2, (unsigned)v);
            else
                printf("%*s ", d_width * 2, "");
        }
        ascii[16] = 0;
        printf(" *%s*\n", ascii);
    }
    d_next = end;
    return 0;
}

static int parse_hex(const char *s, uint32_t *v)
{
    uint32_t x = 0;
    int n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    for (; *s; s++, n++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else return 0;
        x = x << 4 | (uint32_t)d;
    }
    *v = x;
    return n > 0;
}

int cmd_m(uint32_t addr, int width)
{
    if (width != 1 && width != 2 && width != 4)
        width = 2;
    for (;;) {
        uint32_t v;
        if (width == 1) v = *(volatile uint8_t *)addr;
        else if (width == 2) v = *(volatile uint16_t *)addr;
        else v = *(volatile uint32_t *)addr;
        char prompt[32], buf[16];
        sprintf(prompt, "%08x:  %0*x-", (unsigned)addr, width * 2, (unsigned)v);
        if (shell_readline(prompt, buf, 10, 0) < 0 || buf[0] == '.')
            break;
        if (buf[0]) {
            if (!parse_hex(buf, &v)) {
                printf("not a hex number\n");
                continue;
            }
            if (width == 1) *(volatile uint8_t *)addr = (uint8_t)v;
            else if (width == 2) *(volatile uint16_t *)addr = (uint16_t)v;
            else *(volatile uint32_t *)addr = v;
        }
        addr += (uint32_t)width;
    }
    return 0;
}

/* ---- メモリ ---- */

static uint32_t heap_next;

void *malloc(uint32_t n)
{
    if (!heap_next)
        heap_next = ((uint32_t)__stack_top + 15) & ~15u;
    const uint32_t p = heap_next;
    const uint32_t limit = platform_ram_size() - 0x10000;  /* RAM の終わりの近くは使わない */
    n = (n + 15) & ~15u;
    if (p + n > limit || p + n < p)
        return 0;
    heap_next = p + n;
    return (void *)p;
}

void free(void *p)
{
    (void)p;                            /* 取り出すだけで、戻さない */
}

int cmd_mem(void)
{
    if (!heap_next)
        heap_next = ((uint32_t)__stack_top + 15) & ~15u;
    printf("RAM          0x00000000 - 0x%08x (%u KB)\n",
           (unsigned)platform_ram_size(), (unsigned)(platform_ram_size() >> 10));
    printf("program      0x%08x - 0x%08x\n", (unsigned)_start, (unsigned)__bss_end);
    printf("stack top    0x%08x\n", (unsigned)__stack_top);
    printf("heap next    0x%08x\n", (unsigned)heap_next);
    printf("symbols      %d\n", symtab_count);
    return 0;
}
