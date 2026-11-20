/**
 * @file adb.c
 * @brief ADB キーボードの読み取り (VIA1 のシフトレジスタ経由)
 *
 * Mac II の系統 (SE/30 を含む) では、ADB の送受信回路と VIA1 が次のようにつながっている
 * (Guide to the Macintosh Family Hardware 8 章, Linux の drivers/macintosh/via-macii.c,
 *  MAME の macadb.cpp を参考にした):
 *   VIA1 ポートB ビット4-5: 送受信回路への状態 (00 = コマンド, 01 = 偶数バイト, 10 = 奇数バイト, 11 = 待機)
 *   VIA1 ポートB ビット3  : 送受信回路からの合図 (0 = 有効)。意味は状態によって変わる
 *   VIA1 シフトレジスタ   : 1 バイトずつやり取りする (クロックは送受信回路が CB1 に出す)
 *
 * 1 バイトごとにシフトレジスタ割り込みが入る。1 回のやり取り:
 *   1. シフトレジスタを送信にして、コマンド ($2C = アドレス2 のキーボードへ Talk R0) を書き、状態を「コマンド」
 *   2. 送り終わったら受信にして、状態を「偶数」にする
 *   3. 1 バイト受け取るごとに「偶数」と「奇数」を入れ替える。ビット3 が 0 になったら終わり。
 *      ただし 1 バイト目 (偶数) で 0 なのは「応答なし」、2 バイト目 (奇数) で 0 なのは
 *      別の装置からのサービス要求で、どちらももう 1 バイト進めてから終わる (Linux と同じ)
 *   4. 終わったら、割り込みの中ですぐ次の Talk R0 を送る (1 に戻る)
 *
 * 待機中の送受信回路は、最後の Talk を自分で繰り返して (自動問い合わせ)、データがあると
 * 割り込みで知らせてくる。ところが、その間にホストが新しいコマンドを送ると、回路が抱えていた
 * データは捨てられる (キーボード側では取り出し済みなので、押したキーが消える)。
 * そこで Linux と同じく、やり取りの終わりにすぐ次の問い合わせを送り、待機の時間を作らない。
 * それでも待機中に割り込みが来たら、自動問い合わせのデータとして 3 から受け取る。
 */
#include "adb.h"
#include "via.h"

/* platform.c */
uint16_t irq_disable(void);
void irq_restore(uint16_t sr);

#define PB_STATE_MASK   (0x30)
#define PB_ST_CMD       (0x00)
#define PB_ST_EVEN      (0x10)
#define PB_ST_ODD       (0x20)
#define PB_ST_IDLE      (0x30)
#define PB_ADB_INT      (0x08)      /* 0 = データなし / サービス要求 */

#define ACR_SR_MASK     (0x1C)
#define ACR_SR_IN_EXT   (0x0C)      /* 外部クロック (CB1) で受信 */
#define ACR_SR_OUT_EXT  (0x1C)      /* 外部クロック (CB1) で送信 */

#define CMD_TALK_KBD_R0 (0x2C)      /* アドレス 2 (キーボード), Talk, レジスタ 0 */

enum { A_IDLE, A_CMD, A_READ };
static volatile int state = A_IDLE;
static uint8_t reply[8];            /* [0] = コマンド, [1] 以降 = データ */
static int reply_len;
static int no_reply;                /* 1 バイト目で「応答なし」だった */
volatile uint32_t adb_activity;     /* 割り込みのたびに増える (止まっていないかの確認用) */

/* キーのイベントの待ち行列 (積むのは割り込み, 取り出すのはメインの処理) */
#define EVQ_SIZE (32u)
static volatile uint8_t evq[EVQ_SIZE];
static volatile uint32_t evq_head, evq_tail;

#define ADB_LOG(st, v) ((void)0)
#define ADB_LOGP(st, v) ((void)0)
#define ADB_COUNT(v) ((void)0)

static void push_key(uint8_t code)
{
    if (code == 0xFF)                       /* キーなし */
        return;
    if (!(code & 0x80))
        ADB_COUNT(adb_downs);
    uint32_t next = (evq_head + 1) % EVQ_SIZE;
    if (next == evq_tail)
        return;
    evq[evq_head] = code;
    evq_head = next;
}

int adb_get_event(void)
{
    if (evq_tail == evq_head)
        return -1;
    int v = evq[evq_tail];
    evq_tail = (evq_tail + 1) % EVQ_SIZE;
    return v;
}

static void set_bus_state(uint8_t st)
{
    via_w(via_1, VIA_ORB, (via_r(via_1, VIA_ORB) & ~PB_STATE_MASK) | st);
}

static void set_shift_mode(uint8_t mode)
{
    via_w(via_1, VIA_ACR, (via_r(via_1, VIA_ACR) & ~ACR_SR_MASK) | mode);
}

void adb_init(void)
{
    via_w(via_1, VIA_IER, VIA_IRQ_SR);       /* いったん止める (ビット7 = 0 で禁止) */
    set_shift_mode(ACR_SR_IN_EXT);
    set_bus_state(PB_ST_IDLE);
    (void)via_r(via_1, VIA_SR);
    via_w(via_1, VIA_IFR, VIA_IRQ_SR);
    state = A_IDLE;
    via_w(via_1, VIA_IER, VIA_IRQ_SET | VIA_IRQ_SR);
}

/* Talk R0 を送り始める (割り込みを止めた状態で呼ぶ) */
static void send_talk(void)
{
    set_shift_mode(ACR_SR_OUT_EXT);
    via_w(via_1, VIA_SR, CMD_TALK_KBD_R0);
    set_bus_state(PB_ST_CMD);
    state = A_CMD;
    ADB_COUNT(adb_talks);
    ADB_LOG(0x10, CMD_TALK_KBD_R0);
}

static void start_reading(uint8_t first)
{
    reply[0] = first;
    reply_len = 1;
    no_reply = 0;
    set_bus_state(PB_ST_EVEN);
    state = A_READ;
}

/* 受け取り終わった応答からキーを取り出し、次の問い合わせを送る */
static void finish_reply(void)
{
    if (!no_reply) {
        for (int i = 1; i < reply_len && i <= 2; i++)
            push_key(reply[i]);
    } else {
        ADB_COUNT(adb_nodata);
    }
    set_shift_mode(ACR_SR_IN_EXT);
    (void)via_r(via_1, VIA_SR);
    send_talk();
}

void adb_start(void)
{
    uint16_t sr = irq_disable();
    if (state == A_IDLE)
        send_talk();
    irq_restore(sr);
}

void adb_watchdog(void)
{
    static uint32_t last, since;
    uint32_t now = via_clock();
    uint16_t sr = irq_disable();
    if (adb_activity != last) {
        last = adb_activity;
        since = now;
    } else if (now - since > VIA_HZ / 10u) {    /* 0.1 秒割り込みが来ない: やり直す */
        set_shift_mode(ACR_SR_IN_EXT);
        (void)via_r(via_1, VIA_SR);
        send_talk();
        since = now;
    }
    irq_restore(sr);
}

void adb_irq(void)
{
    uint8_t pb = via_r(via_1, VIA_ORB);
    int line = !(pb & PB_ADB_INT);          /* 回路からの合図 */
    uint8_t v;

    adb_activity++;
    switch (state) {
    case A_CMD:                             /* コマンドを送り終えた -> 受信にして応答を受け取る */
        ADB_LOGP(A_CMD, 0);
        set_shift_mode(ACR_SR_IN_EXT);
        (void)via_r(via_1, VIA_SR);
        /* ここで合図が出ていれば、続くのは回路が先に自動問い合わせで受けたデータ (同じくキーボードの R0) */
        start_reading(CMD_TALK_KBD_R0);
        break;
    case A_READ:
        v = via_r(via_1, VIA_SR);
        ADB_LOGP(A_READ, v);
        if (line) {
            if (reply_len == 1 && (pb & PB_STATE_MASK) == PB_ST_EVEN) {
                no_reply = 1;               /* 応答なし */
            } else if (reply_len == 2 && (pb & PB_STATE_MASK) == PB_ST_ODD) {
                /* 別の装置 (マウスなど) からのサービス要求。キーボードしか読まないので気にしない */
            } else {
                finish_reply();
                break;
            }
        }
        if (reply_len < (int)sizeof reply)
            reply[reply_len++] = v;
        set_bus_state((pb & PB_STATE_MASK) ^ PB_STATE_MASK);   /* 偶数 <-> 奇数 */
        break;
    default:                                /* 待機中: 自動問い合わせのデータが来た */
        v = via_r(via_1, VIA_SR);
        ADB_LOGP(A_IDLE, v);
        ADB_COUNT(adb_autos);
        start_reading(v);
        break;
    }
}
