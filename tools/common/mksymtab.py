"""nm の出力からシンボルテーブル (C のソース) を作る (標準ライブラリだけで動く)。

  m68k-linux-gnu-nm stage2.elf > syms.txt
  python3 tools/common/mksymtab.py syms.txt symtab.c

外から見えるシンボル (nm の型が大文字の T, D, B, R) だけを、名前の順に並べる。
'_' 2 つで始まるシンボル (リンカが作るもの) は除く。
シェルのコマンドの関数 (cmd_ で始まる関数) は、cmd_ を外した名前でも登録する (型 'C')。
シェルでは cmd_help も help も同じ関数を指す。
引数を 1 つだけ渡すと、空の表を作る (1 回目のリンク用)。
"""
import sys

KEEP = {"T", "D", "B", "R"}
CMD_PREFIX = "cmd_"


def read_syms(path):
    syms = {}
    for line in open(path, encoding="utf-8"):
        parts = line.split()
        if len(parts) != 3:
            continue
        addr, typ, name = parts
        if typ not in KEEP or name.startswith("__"):
            continue
        syms[name] = (int(addr, 16), typ)
    # コマンドの短い名前
    for name, (addr, typ) in list(syms.items()):
        if typ == "T" and name.startswith(CMD_PREFIX) and len(name) > len(CMD_PREFIX):
            short = name[len(CMD_PREFIX):]
            if short in syms:
                raise SystemExit(f"{path}: コマンド {name} の短い名前 {short} がほかのシンボルとぶつかる")
            syms[short] = (addr, "C")
    return syms


def main():
    if len(sys.argv) == 2:
        syms, out = {}, sys.argv[1]
    else:
        syms, out = read_syms(sys.argv[1]), sys.argv[2]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* 自動生成 (tools/common/mksymtab.py)。書き換えないこと */\n")
        f.write('#include "symtab.h"\n\n')
        f.write("const struct symtab_entry symtab_table[] = {\n")
        for name in sorted(syms):
            addr, typ = syms[name]
            f.write(f'    {{ "{name}", 0x{addr:08X}u, \'{typ}\' }},\n')
        f.write('    { 0, 0, 0 }\n};\n')
        f.write(f"const int symtab_count = {len(syms)};\n")


if __name__ == "__main__":
    main()
