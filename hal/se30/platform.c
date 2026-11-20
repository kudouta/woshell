/**
 * @file platform.c
 * @brief SE/30 の初期化、起動し直し、RAM の大きさ (platform.h)
 *
 * 割り込みは irq.c、VIA と時計は via.c、ROM の起動のアイコンは romicon.c にある。
 */
#include "platform.h"
#include "mac.h"
#include "via.h"
#include "irq.h"
#include "adb.h"

void platform_init(void)
{
    via_init();
    irq_init();                         /* 例外と割り込みのベクタ */
    adb_init();                         /* ADB (VIA1 のシフトレジスタ割り込み) */
    irq_enable();                       /* 垂直同期の割り込みを許可し、割り込みを受け付け始める */
}

/* ROM の先頭 ($40800000) の 4 バイト目に、リセットしたときに始める番地がある。
   reset 命令で周りの回路を初期化してから、そこへ飛ぶ */
void platform_reboot(void)
{
    __asm__ volatile(
        "ori.w   #0x0700, %%sr\n\t"
        "movea.l 0x40800004, %%a0\n\t"
        "reset\n\t"
        "jmp     (%%a0)" ::: "a0", "memory");
    __builtin_unreachable();
}

uint32_t platform_ram_size(void)
{
    return MemTop;
}
