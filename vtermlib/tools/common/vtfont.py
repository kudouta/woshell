"""テキストで描いたビットマップのフォントを C のヘッダにする (標準ライブラリだけで動く)。

  python3 tools/common/vtfont.py assets/font/vt6x12.txt build/font.h

フォントのファイル (書き方は assets/font/vt6x12.txt の先頭にもある):
  size W H        1 文字のます目の幅 (1-8) と高さ
  underline N     下線を引く行 (0 から)
  [c]             次の H 行が ASCII の文字 c (0x20-0x7E)
  {c}             次の H 行が DEC の線画文字 c (0x5F-0x7E)
  形の 1 行は '#' (点灯) と '.' (消灯) を W 個。';' で始まる行と空行は読み飛ばす
出力: font_glyph[文字の番号][行] (uint8_t, 最上位ビットが左端)。
      文字の番号は ASCII ならその値、線画文字は 0x80 + (c - 0x5F)。
      書いてない ASCII は空白、書いてない線画文字は同じ c の ASCII の形になる
"""
import sys

DEC_BASE = 0x80


def load(path):
    w = h = ul = None
    glyphs = {}                 # 文字の番号 -> 行の並び
    cur, rows = None, []
    for n, raw in enumerate(open(path, encoding="utf-8"), 1):
        line = raw.rstrip("\n")
        where = f"{path}:{n}"
        if cur is None:
            s = line.strip()
            if not s or s.startswith(";"):
                continue
            words = s.split()
            if words[0] == "size" and len(words) == 3:
                w, h = int(words[1]), int(words[2])
                if not (1 <= w <= 8 and 1 <= h <= 32):
                    raise SystemExit(f"{where}: size は幅 1-8、高さ 1-32")
                continue
            if words[0] == "underline" and len(words) == 2:
                ul = int(words[1])
                continue
            if len(line) == 3 and line[0] + line[2] in ("[]", "{}"):
                if w is None:
                    raise SystemExit(f"{where}: 文字の前に size を書くこと")
                c = ord(line[1])
                if line[0] == "[":
                    if not 0x20 <= c <= 0x7E:
                        raise SystemExit(f"{where}: [ ] の文字は 0x20-0x7E")
                    cur = c
                else:
                    if not 0x5F <= c <= 0x7E:
                        raise SystemExit(f"{where}: {{ }} の文字は 0x5F-0x7E")
                    cur = DEC_BASE + c - 0x5F
                if cur in glyphs:
                    raise SystemExit(f"{where}: {line} が 2 回ある")
                rows = []
                continue
            raise SystemExit(f"{where}: 読めない行: {line!r}")
        if len(line) != w or set(line) - set("#."):
            raise SystemExit(f"{where}: 形の行は '#' と '.' を {w} 個: {line!r}")
        rows.append(sum(1 << (7 - i) for i, ch in enumerate(line) if ch == "#"))
        if len(rows) == h:
            glyphs[cur] = rows
            cur = None
    if cur is not None:
        raise SystemExit(f"{path}: 最後の文字の行が足りない")
    if w is None:
        raise SystemExit(f"{path}: size がない")
    if ul is None or not 0 <= ul < h:
        raise SystemExit(f"{path}: underline (0-{h - 1}) がない")
    return w, h, ul, glyphs


def main():
    src, out = sys.argv[1:3]
    w, h, ul, glyphs = load(src)
    table = []
    for i in range(256):
        if i in glyphs:
            table.append(glyphs[i])
        elif DEC_BASE <= i < DEC_BASE + 32:
            table.append(glyphs.get(i - DEC_BASE + 0x5F, [0] * h))
        else:
            table.append([0] * h)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"/* 自動生成 (tools/common/vtfont.py, {src} から)。書き換えないこと */\n")
        f.write("#ifndef FONT_H_INCLUDED\n#define FONT_H_INCLUDED\n#include <stdint.h>\n\n")
        f.write(f"#define FONT_W ({w})\n#define FONT_H ({h})\n#define FONT_UNDERLINE ({ul})\n")
        f.write(f"#define FONT_DEC_BASE (0x{DEC_BASE:02X})    /* 線画文字 c の番号 = FONT_DEC_BASE + c - 0x5F */\n\n")
        f.write("/* [文字の番号][行] = 点灯 (最上位ビットが左端)。使う側で FONT_GLYPHS を定義したときだけ入れる */\n")
        f.write("#ifdef FONT_GLYPHS\n")
        f.write("static const uint8_t font_glyph[256][FONT_H] = {\n")
        for i, rows in enumerate(table):
            if not any(rows) and i not in glyphs:
                continue
            if 0x20 <= i <= 0x7E:
                name = repr(chr(i)).replace("*/", "* /")
            elif DEC_BASE <= i < DEC_BASE + 32:
                name = "DEC " + repr(chr(i - DEC_BASE + 0x5F)).replace("*/", "* /")
            else:
                name = ""
            f.write(f"    [0x{i:02X}] = {{ " + ", ".join(f"0x{r:02X}" for r in rows) + " },   /* " + name + " */\n")
        f.write("};\n#endif\n\n#endif\n")
    print(f"{out}: {w} x {h} ドット, 文字 {len(glyphs)}")


if __name__ == "__main__":
    main()
