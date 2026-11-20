# woshell

Macintosh SE/30 専用のベアメタル実装評価用に作った簡易shellです。woshellの使い方は `src/README.md` を参照願います。

## English summary

- Tested on Windows 11 with WSL (Ubuntu). A native Linux machine should also work (run `make` instead of `wsl make`)
- Needs `m68k-linux-gnu-gcc`, GNU make 4.3+ and Python 3 in WSL
- `wsl make` builds `build/woshell1440.img`, a bootable 1.44MB floppy image for the Macintosh SE/30
- Run the image in MAME (`macse30`) or write it to a real 1.44MB floppy. **No Apple ROM is included**; supply your own ROM set for MAME
- Documentation is written in Japanese
- License: MIT. Apple, Macintosh and SE/30 are trademarks of Apple Inc. This project is not affiliated with Apple

## 必要なもの

Windows 11 の WSL（Ubuntu 22.04 以降）で確認しています。Linux の実機でも、おそらく同じように作業できます（`wsl make` の代わりに `make` を実行します）。

- WSL の Ubuntu
- MAME（`macse30` が動くもの）。`mame` の名前で起動できるようにしておきます
- SE/30で動かすなら: Macintosh SE/30 本体、Windows の Python、USB の 1.44MB フロッピードライブ。純正 ROM でのみ動作確認しています（改造 ROM での動作は保証していません）。
- **SE/30 の ROM はこのリポジトリーに含みません**。MAME で使う ROM は自分で用意してください

## 手順

1. WSL の Ubuntu に必要なパッケージをインストールします。

   ```
   sudo apt install gcc-m68k-linux-gnu make python3 gcc
   ```

2. Windows のコマンドプロンプトか PowerShell で、このフォルダーに移ってビルドします。`build/woshell1440.img`（1,474,560 バイト）ができます。

   ```
   wsl make
   ```

3. MAME で動かします。プロンプト `-> ` が出れば動いています。

   ```
   mame macse30
   ```

4. SE/30で動かすときは、管理者のコマンドプロンプトでフロッピーに書き、SE/30 に入れて起動します。

   ```
   python hal/tools/se30/writefloppy.py build/woshell1440.img
   ```

そのほかのターゲット:

```
wsl make test           # 端末ライブラリー (vtermlib) を PC で確認
wsl make clean          # build/ および hal、vtermlib、libc の build/ をクリーン
wsl make -C vtermlib    # 端末デモ vtermlib/build/vterm1440.img を生成
```

`make` に渡せるパラメーター:

| 変数 | 省略時 | 意味 |
|---|---|---|
| `CROSS` | `m68k-linux-gnu-` | クロスコンパイラーの接頭辞 |
| `MACHINE` | `se30` | 機種（`hal/<機種>` を使う）。`se30` 固定 |
| `FONT` | `assets/font/vt6x12.txt` | 端末で使用するフォント定義（テキスト）。`vtermlib/` からの相対パスで書く。文字の大きさが変わると、画面の桁数と行数も変わる |
| `EXTRA_CFLAGS` | なし | 追加のコンパイラーオプション |
| `HOSTCC` | `gcc` | `make test` で使うホストコンパイラー |

### 生成物

| ファイル | 内容 |
|---|---|
| `build/woshell1440.img` | 1.44MB のフロッピーイメージ（ブートブロック + プログラム） |
| `build/stage2.elf` | リンクしたプログラム。`-g` 付き（gdb、addr2line、`objdump -S` 用） |
| `build/stage2.bin` | フロッピーに入れるプログラム（`stage2.elf` から情報を落としたもの） |
| `build/stage2.lst` | 逆アセンブルとソースを並べたもの |
| `build/syms.txt` | シンボルの一覧（`nm` の結果） |
| `hal/build/libse30.a`、`crt0.o`、`boot.bin` | SE/30依存レイヤー、スタートアップ（エントリーポイント）、ブートブロック |
| `vtermlib/build/libvterm.a` | 端末ライブラリー（フォントも入る） |
| `libc/build/libc.a` | 自前の小さな C ライブラリー |

## 動かすときの補足

- **MAME**: `mame macse30` はこのフォルダーの `macse30.ini` を読み、`build/woshell1440.img` をフロッピーに入れて起動します。端末デモは `mame macse30 -flop1 vtermlib/build/vterm1440.img` で動きます。
- **実機**: 書いたフロッピーは Windows からは読めない形式になります。一度も初期化していないディスクは書けないことがあるので、先に Windows で初期化しておきます。

MAME の SE/30 は音源（ASC）が「不完全」とされていて、起動の前に警告の画面が出ます。`ui.ini` の設定で、14 日以内に一度見ていれば出さないようにしてあります。

MAME でシリアルを使うときは、下記のように起動します。両方のポートが TCP の待ち受けになります（モデムポートが 2333 番、プリンターポートが 2334 番）。shellは起動のときモデムポートを使うので、`python tools/common/serterm.py` か telnet の端末で 2333 番につなぎます。なお MAME の SE/30 では、モデムポートのチャンネル A の名前が `printer` になっています。

```
mame macse30 -printer null_modem -bitb1 socket.127.0.0.1:2333 -modem null_modem -bitb2 socket.127.0.0.1:2334
```

## 実機で確認した項目

2026-11-20 に、コミット `d88655a` のフロッピーで確認しました。本体は Macintosh SE/30（純正 ROM、RAM 20MB、内蔵の電池は切れている）です。シリアルケーブルはつないでいません。

| 項目 | 結果 |
|---|---|
| 起動 | フロッピーから起動し、ROM の起動アイコンと 3 行のメッセージが出る。`RAM 8192 KB`（ROM が見つけた 8MB） |
| 式と変数、エラーの表示 | OK。知らない名前は `undefined symbol` を出してプロンプトに戻る |
| エスケープシーケンス | `printf` で太字、反転、下線、倍幅の行が出る |
| `wo` | 約 3 秒で右端から左端へ泳ぐ |
| `beep` | 音が鳴る |
| `d`、`irqs`、`mmu` | OK。MMU の設定と変換テーブルは MAME と同じ（24 ビットのモード） |
| `date`、`setdate` | OK。電池が切れているので、電源を入れると 1904-01-01 から数え始める |
| `pram`、`pramw` | OK。`reboot` しても値は残る。電源を切ると ROM の初期値に戻る |
| `serial` | ケーブルなしでも送信の割り込みで送り続ける（受信 0 バイト、誤り 0） |
| `scrbench` | 画面の読み出し 17.4ms、書き込み 12.0ms、移動 22.0ms（342 行）。文字を流す速さは、シリアルにも出すと 13 行/秒（9600bps が上限）、`serout 0` で 61 行/秒（このときのテストの行は 87 文字で、2 行に折り返していた。今は 80 文字） |
| 例外からの復帰 | `(1)()`（奇数アドレスの呼び出し）でアドレスエラーを出し、プロンプトに戻る |
| キーボード | 矢印、Control キー（A / E / B / F / D / K / U / P / N / C / L）、履歴、キーリピート、Shift、Caps Lock |
| 割り込みボタン | `delay`、`wo` の途中で押すとプロンプトに戻る。1 回の押下で約 12,000 回入る NMI（約 13µs ごと）を 1 回と数える |
| `reboot` | ROM から起動し直す |

**まだ確認していないこと、うまく動かないこと**

- シリアルの送受信（ケーブルができてから）、115200 / 230400bps
- バスエラーからの復帰。何もつながっていないスロットの空間（`*0xF9000000`）を読んでもバスエラーにならないため、試せていません
- `ramscan` は実機では止まります。物理 8〜16MB の範囲だけなら動きますが、16MB より上を調べると止まります。8MB より上の RAM を使うには、System 7 の MODE32 と同じように、自前の MMU テーブルを作る必要があると考えています
- PRAM の `$10` 番地は、ROM が PRAM の中身が正しいかを見る印（`$A8`）です。書き換えると、起動し直したときに ROM が PRAM を初期化します

## build時の注意

1. `stage2.elf` から `stage2.bin` を作り、`hal/tools/se30/mkfloppy.py` でブートブロックと一緒にフロッピーイメージにします。
2. リンクの順は `crt0.o`、shell、シンボルテーブル、`-lvterm -lse30 libc/build/libc.a -lgcc` です。`libvterm` は `libse30` を使い、どれも `libc` を使います。`libc.a` を `-lc` でなくパスで渡すわけは `libc/README.md` にあります。

**シンボルテーブル**: シェルからプログラムの関数や変数を名前で呼べるよう、シンボル（`nm` の T / D / B / R）をテーブルにしてプログラムに入れます。テーブルを入れると大きさが変わってアドレスがずれるので、次のように 3 回リンクします。

1. 空のテーブルでリンクし、シンボルの名前を集めます。
2. 1 の名前でテーブルを作ってリンクします（テーブルの大きさは最後と同じになります。ただしテーブルに入るアドレスは、まだ 1 のリンクのものです）。
3. 2 のアドレスでテーブルを作ってリンクします。2 と 3 のアドレスが同じことを確認し、違えば止まります。

テーブルは `tools/common/mksymtab.py` が作ります。`cmd_` で始まる関数は、`cmd_` を外した名前でもテーブルに入ります。

## フォルダー

上位のレイヤーは、下位のレイヤーのヘッダーのみを使います。

```
src/       -> build/woshell1440.img         シェル
  |
vtermlib/  -> libvterm.a                    VT102 相当の端末 (機種に依存しない)
  |
hal/       -> libse30.a, crt0.o, boot.bin   機種のレイヤー (インターフェースのヘッダーと SE/30 の実装)

libc/      -> libc.a                        自前の小さな C ライブラリー (どのレイヤーからも使う)
```

| フォルダー | 内容 |
|---|---|
| `src/` | シェル。くわしくは `src/README.md` |
| `vtermlib/` | 端末ライブラリー、フォント、デモ、PC のテスト。くわしくは `vtermlib/README.md` |
| `hal/` | インターフェースのヘッダー（`hal/include`）、SE/30 の実装（`hal/se30`）、68030 の部分（`hal/m68030`）、フロッピーを作る・書くツール（`hal/tools/se30`）。くわしくは `hal/README.md` |
| `libc/` | `memcpy` / `memset`（GCC が呼ぶので要る）と `printf` / `sprintf`。くわしくは `libc/README.md` |
| `tools/common/` | `mksymtab.py`（シンボルテーブルを作る）、`serterm.py`（MAME のシリアルにつなぐ端末） |
| `macse30.ini`、`ui.ini` | MAME の設定（場所はこのフォルダーからの相対） |

## ソースの決まり

リポジトリー全体（`src/`、`vtermlib/`、`hal/`、`libc/`）で同じ決まりを使います。

**置き場所**
- 機種に依存する部分はすべて `hal/` に置きます。`src/` と `vtermlib/` は `hal/include` のインターフェースだけを使い、レジスタやアドレスに触りません。
- C の標準ライブラリーにある関数（`memcpy`、`printf` など）は `libc/` に置きます。

**名前**
- 外から見える名前（`static` でない関数と変数）は `モジュール名_` で始めます。モジュール名はファイル名にそろえます（`vtdraw.c` なら `vtdraw_`）。
- 機種に依存しないインターフェース（`hal/include/*.h`）の関数は、インターフェースの名前で始めます（`video_`、`keyboard_`、`timer_`、`beep_`、`platform_`、`serial_`、`rtc_`）。実装がどのファイルにあってもこの名前にします（`timer_now` は `via.c`、`platform_irq_count` は `irq.c`）。
- SE/30 の中だけで使うものは、その部品の名前で始めます（`via_`、`irq_`、`adb_`、`asc_`、`trap_`）。
- マクロと定数は大文字で、同じ接頭辞を付けます（`VTCORE_COLS`、`VTCORE_ATTR_BOLD`、`KEYBOARD_UP`）。型も同じ接頭辞を付けます（`struct vtcore_cell`）。
- アセンブラーのエントリーは `モジュール名_…_entry` にします（`trap_exc_entry`、`irq_level1_entry`）。
- `#define` の値は、数 1 つでもかっこでくくります（`#define VIA_IRQ_SR (0x04)`、`#define VTCORE_COLS (VIDEO_W / FONT_W)`）。アセンブラー（`.S`）の定義も同じです。かっこでくくらない例外は次のものです。
  - 文字列（つなげて使うので、かっこを付けると壊れる。`#define MACHINE_NAME "SE/30"`）
  - 属性や名前を作るマクロ
  - 値のない印
  - 文の形のマクロ
- `static` の関数と変数には接頭辞を付けません。名前は小文字とアンダースコアで付けます。
- 接頭辞を付けるルールの例外: C の標準の名前（`printf`、`memcpy`、`malloc`、`main` など）と、Mac のローメモリ変数（`MemTop`、`ROMBase` など。Inside Macintosh の名前のまま）
- 使う側が用意する関数は、それが属するインターフェースの名前で始めます（`trap_fatal`、`fmt_putc`、`serial_irq`）。

**シェルのコマンド**
- C の関数は `cmd_` で始めます（`cmd_help`、`cmd_d`）。
- コマンドを足すときは、`src/cmds.c` に `cmd_名前` の関数を、`src/cmds.h` に宣言を、`src/cmds.c` の `help_items` に説明を足します。

**コメント**
- 外から使う関数は、ヘッダーの宣言の前に Doxygen の形（`@brief`、`@param`、`@return`）で説明を書きます。`.c` の定義には書きません（二重に書くと食い違いやすいため）。
- `static` の関数は、定義の前に 1〜2 行で何をするかを書きます。
- ファイルの先頭には `@file` と `@brief` を書き、続けてそのファイルの役目としくみを書きます。

## ライセンス

MIT License（`LICENSE`）。

Apple、Macintosh、SE/30 は Apple Inc. の商標です。このプロジェクトは Apple とは関係ありません。
