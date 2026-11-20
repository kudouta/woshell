/**
 * @file romicon.c
 * @brief ROM の中の起動のアイコン (platform_startup_icon, platform.h)
 *
 * ROM がフロッピーなどから起動するときに画面の中央に出す 32 x 32 のアイコンを、ROM から読む。
 * 絵そのものはこのプログラムに入れず、動いている機械の ROM から読むだけにする。
 *
 * 場所: SE/30 の ROM (先頭の 4 バイトのチェックサムが $97221136) の先頭から $1948 に絵 (128 バイト、
 * 1 行 4 バイトで 32 行、最上位ビットが左端)、続く $19C8 にマスク (同じ形) がある。
 * MAME の起動の画面に出ているアイコンと ROM の中の並びを照らし合わせて見つけた (2026-11-19)。
 * 絵とマスクで描くと、ROM が描いた画面と 1 ドットも違わない。
 * ほかの ROM では場所が違うので、チェックサムが合わなければ読まない。
 */
#include "platform.h"
#include "mac.h"

#define SE30_ROM_CHECKSUM (0x97221136u)
#define ICON_OFFSET       (0x1948u)

int platform_startup_icon(uint32_t bits[32])
{
    const volatile uint32_t *rom = (const volatile uint32_t *)ROMBase;
    if (rom == 0 || rom[0] != SE30_ROM_CHECKSUM)
        return 0;
    const volatile uint32_t *icon = (const volatile uint32_t *)((uint32_t)rom + ICON_OFFSET);
    for (int i = 0; i < 32; i++)
        bits[i] = icon[i];
    return 1;
}
