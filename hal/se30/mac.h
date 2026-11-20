/**
 * @file mac.h
 * @brief SE/30 のローメモリ変数 (SE/30 の中だけで使う)
 *
 * ローメモリ変数は、Mac の ROM が RAM の先頭近くに置く値。名前は Inside Macintosh のものに合わせ、
 * 命名規則の例外として頭を付けない (ScreenRow, MemTop など)。
 */
#ifndef MAC_H
#define MAC_H

#include <stdint.h>

/** 16 ビットのローメモリ変数 */
#define MAC_LOWMEM_W(addr) (*(volatile uint16_t *)(addr))
/** 32 ビットのローメモリ変数 */
#define MAC_LOWMEM_L(addr) (*(volatile uint32_t *)(addr))

#define ScreenRow   (MAC_LOWMEM_W(0x0106)) /**< 画面の 1 行あたりのバイト数 */
#define MemTop      (MAC_LOWMEM_L(0x0108)) /**< ROM が見つけた物理 RAM の終わり (24 ビットのモードでも全量) */
#define BufPtr      (MAC_LOWMEM_L(0x010C)) /**< 使える RAM の終わり (24 ビットのモードでは 8MB の窓の中) */
#define BootDrive   (MAC_LOWMEM_W(0x0210)) /**< 起動ドライブ番号 */
#define SCCRd       (MAC_LOWMEM_L(0x01D8)) /**< SCC の番地 (読む側。SE/30 は書く側も同じ) */
#define ROMBase     (MAC_LOWMEM_L(0x02AE)) /**< ROM の先頭 */
#define ScrnBase    (MAC_LOWMEM_L(0x0824)) /**< フレームバッファの先頭 */

#endif
