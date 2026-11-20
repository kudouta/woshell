/**
 * @file rtc.c
 * @brief SE/30 の時計 (Apple 343-0042-B: 時計と 256 バイトの PRAM)
 *
 * VIA1 ポートB の 3 本でつながっている:
 *   ビット0  データ (読むときは入力に切り替える)
 *   ビット1  クロック
 *   ビット2  選択 (0 で話を始める、1 で終わる)
 * 1 回のやり取りは「選択を 0 → 命令 1 バイト → (書くなら) データ 1 バイトを送る / (読むなら) 1 バイト受ける
 * → 選択を 1」。どれも上位ビットから。
 *
 * 1 ビットの送り方: クロックを 0 にしながらデータを出し、クロックを 1 にする
 *   (時計はクロックの変わり目で受け取る。MAME は 1 → 0 で、ここはどちらでも同じデータが見える)。
 * 1 ビットの受け方: クロックを 0、1 の順に動かしてからデータを読む (時計は 1 → 0 で次のビットを出す)。
 *
 * 命令 (先頭のビット z が 1 で読む、0 で書く):
 *   z00aa001        秒 (aa = 0 が最下位バイト ... 3 が最上位)。例: 読む 0x81, 0x85, 0x89, 0x8D
 *   00110101        書き込み禁止 (データのビット7 が 1 で禁止)。書く前に外す
 *   z0111aaa 0bbbbb00  PRAM の aaabbbbb 番地 (2 バイトの命令)
 * 古い 20 バイトの PRAM の番地は、新しい番地では 0x10-0x1F (旧 0x00-0x0F) と 0x08-0x0B (旧 0x10-0x13)。
 *
 * ADB と垂直同期の割り込みも同じポートB を書き換えるので、やり取りの間は割り込みを止める
 * (1 回 100us ほど)。
 */
#include "rtc.h"
#include "via.h"

uint16_t irq_disable(void);         /* platform.c */
void irq_restore(uint16_t sr);

#define PB_DATA (0x01)
#define PB_CLK  (0x02)
#define PB_ENB  (0x04)

#define CMD_READ       (0x80)
#define CMD_SECONDS(n) (0x01 | (n) << 2)
#define CMD_WPROTECT   (0x35)
#define CMD_XPRAM      (0x38)

static uint8_t pb;                  /* やり取りの間のポートB の値 */

/* ポートB に書く。VIA は 1 回の読み書きに約 1us かかるので、読み直してもう 1 回分待つ */
static void out(void)
{
    via_w(via_1, VIA_ORB, pb);
    (void)via_r(via_1, VIA_ORB);
}

static uint16_t begin(void)
{
    const uint16_t sr = irq_disable();
    via_w(via_1, VIA_DDRB, via_r(via_1, VIA_DDRB) | PB_DATA | PB_CLK | PB_ENB);
    pb = via_r(via_1, VIA_ORB) | PB_CLK | PB_ENB;    /* クロック 1 で待つ */
    out();
    pb &= ~PB_ENB;
    out();
    return sr;
}

static void end(uint16_t sr)
{
    pb |= PB_ENB;
    out();
    irq_restore(sr);
}

static void send_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        pb = (pb & ~(PB_CLK | PB_DATA)) | ((b >> i) & 1);
        out();
        pb |= PB_CLK;
        out();
    }
}

static uint8_t recv_byte(void)
{
    via_w(via_1, VIA_DDRB, via_r(via_1, VIA_DDRB) & ~PB_DATA);
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        pb &= ~PB_CLK;
        out();
        pb |= PB_CLK;
        out();
        v = (uint8_t)(v << 1 | (via_r(via_1, VIA_ORB) & PB_DATA));
    }
    via_w(via_1, VIA_DDRB, via_r(via_1, VIA_DDRB) | PB_DATA);
    return v;
}

static uint8_t read_reg(uint8_t cmd)
{
    const uint16_t sr = begin();
    send_byte(cmd | CMD_READ);
    const uint8_t v = recv_byte();
    end(sr);
    return v;
}

static void write_reg(uint8_t cmd, uint8_t v)
{
    const uint16_t sr = begin();
    send_byte(cmd);
    send_byte(v);
    end(sr);
}

static void write_protect(int on)
{
    write_reg(CMD_WPROTECT, on ? 0xD5 : 0x55);
}

static uint32_t read_seconds(void)
{
    uint32_t s = 0;
    for (int n = 3; n >= 0; n--)
        s = s << 8 | read_reg(CMD_SECONDS(n));
    return s;
}

uint32_t rtc_get(void)
{
    /* 4 バイトを読む間に繰り上がると食い違うので、2 回続けて同じになるまで読む */
    uint32_t a = read_seconds(), b;
    for (int tries = 0; tries < 4 && (b = read_seconds()) != a; tries++)
        a = b;
    return a;
}

int rtc_set(uint32_t secs)
{
    write_protect(0);
    for (int n = 0; n < 4; n++)
        write_reg(CMD_SECONDS(n), (uint8_t)(secs >> (8 * n)));
    write_protect(1);
    const uint32_t now = rtc_get();
    return now - secs <= 1 ? 0 : -1;    /* 書いた直後に 1 秒進むことはある */
}

uint8_t rtc_pram_read(int addr)
{
    const uint16_t sr = begin();
    send_byte(CMD_READ | CMD_XPRAM | ((addr >> 5) & 7));
    send_byte((uint8_t)((addr & 0x1F) << 2));
    const uint8_t v = recv_byte();
    end(sr);
    return v;
}

void rtc_pram_write(int addr, uint8_t v)
{
    write_protect(0);
    const uint16_t sr = begin();
    send_byte(CMD_XPRAM | ((addr >> 5) & 7));
    send_byte((uint8_t)((addr & 0x1F) << 2));
    send_byte(v);
    end(sr);
    write_protect(1);
}
