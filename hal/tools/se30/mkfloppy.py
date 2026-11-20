"""生セクタ形式のフロッピーイメージを作る (既定 800KB。4 つ目の引数に 1440 で 1.44MB)。

  1.44MB (MFM) は PC のフロッピーと同じ記録方式なので、PC の USB フロッピードライブで書き込める。
  SE/30 の ROM と SuperDrive は 1.44MB のディスクからも起動できる。

  ブロック 0-1 : ブートブロック (boot.bin)
  ブロック 2-  : ステージ2 (stage2.bin)

ブートブロック末尾の stage2_offset / stage2_size を書き換えて、
ステージ1がステージ2を読み込めるようにする。
"""
import struct
import sys

DISK_SIZES = {"800": 800 * 1024, "1440": 1440 * 1024}
BLOCK = 512
BOOT_SIZE = 1024
STAGE2_OFFSET = BOOT_SIZE
PATCH_AT = 0x3F8  # boot.S の stage2_offset


def main():
    boot_path, stage2_path, out_path = sys.argv[1:4]
    disk_size = DISK_SIZES[sys.argv[4] if len(sys.argv) > 4 else "800"]
    boot = bytearray(open(boot_path, "rb").read())
    stage2 = open(stage2_path, "rb").read()

    if len(boot) != BOOT_SIZE:
        sys.exit(f"boot.bin は {BOOT_SIZE} バイトのはずが {len(boot)} バイト")
    if boot[:2] != b"LK":
        sys.exit("boot.bin の先頭が 'LK' ではない")

    size = (len(stage2) + BLOCK - 1) // BLOCK * BLOCK
    if STAGE2_OFFSET + size > disk_size:
        sys.exit("ステージ2がディスクに収まらない")
    struct.pack_into(">II", boot, PATCH_AT, STAGE2_OFFSET, size)

    img = bytearray(disk_size)
    img[0:BOOT_SIZE] = boot
    img[STAGE2_OFFSET:STAGE2_OFFSET + len(stage2)] = stage2
    open(out_path, "wb").write(img)
    print(f"{out_path}: stage2 {len(stage2)} バイト ({size // BLOCK} ブロック)")


if __name__ == "__main__":
    main()
