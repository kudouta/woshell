# VTermLib — VT102 相当の文字端末ライブラリー

エスケープシーケンスを混ぜた文字を出すだけで画面を操れる、コンソールのライブラリー（静的ライブラリー `.a`）です。カーソル移動、消去、文字の飾り、スクロール範囲、倍幅・倍高の行などが使えます。DEC VT102 の動きに、xterm のよく使われる機能を少し足したものです。シリアルでつなぐ端末とは違い、端末から見た「ホスト」は、このライブラリーを使うプログラム自身になります。

機種には依存しません。画面、キーボード、時計、ベルは、機種のレイヤー（`../hal`）のインターフェースのヘッダー（`video.h`、`keyboard.h`、`timer.h`、`beep.h`）を通して使います。

使えるもの:

- 文字は 6 x 12 ドット（12 ポイント相当）で、画面は **85 桁 x 28 行**です。フォントはテキストのファイルで、書き換えたり差し替えたりできます（大きさを変えると桁数と行数も変わります）。
- 太字、下線、反転、点滅、見えない文字。DEC の線画文字
- 倍幅の行、倍高の行（DECDWL / DECDHL）
- ジャンプスクロールとスムーズスクロール（DECSCLM）
- ベル（BEL）で ASC から短い音
- xterm の「代わりの画面」（もう 1 枚の画面に切り替える機能。?1049 など）、カーソルの形（DECSCUSR）

| 作るもの | 内容 |
|---|---|
| `build/libvterm.a` | 端末。`vt.o`、`vtcore.o`、`vtdraw.o`。フォントもここに入る |
| `build/vterm1440.img` | デモ。`demo/main.c` を、`libvterm.a`、機種のレイヤーの `libse30.a`、`libc.a` とリンクしたもの |

## 作り方

Linux では `make`、Windows では `wsl make` のように WSL から、このフォルダーで実行します。

```
make                # ライブラリーとデモ (build/vterm1440.img, 1.44MB のフロッピー。機種のレイヤーと libc も作る)
make lib            # ライブラリーだけ
make test           # PC で端末の中身を確認する
```

実機で動かすときは、このフォルダーで、Windows の管理者のコマンドプロンプトから `python ../hal/tools/se30/writefloppy.py build/vterm1440.img` を実行してフロッピーに書きます。

MAME では、woshell のフォルダーで `mame macse30 -flop1 vtermlib/build/vterm1440.img` を実行します。SE/30 の ROM は自分で用意してください。

別のフォントを使うときは `make FONT=assets/font/ほかのファイル.txt` とします。桁数と行数は、フォントの大きさからコンパイルのときに決まります。そのためフォントは `libvterm.a` の中に組み込まれます。フォントを変えたらライブラリーを作り直してください。

## デモ

起動するとメニューが出ます。数字のキーで選びます。

| キー | 内容 |
|---|---|
| 1 | 文字の飾り（SGR）の組み合わせ、ASCII、DEC の線画文字、SO/SI、英国の文字の組 |
| 2 | 線画文字で描いた枠と表 |
| 3 | 倍幅・倍高の行（飾り付き、点滅付き） |
| 4 | スクロール範囲、ジャンプとスムーズのスクロール、RI、SU / SD |
| 5 | 文字・行の挿入と削除（ICH, DCH, IL, DL, ECH, IRM）、EL |
| 6 | ベル |
| 7 | キーが送るバイト列を 16 進で表示。1 = DECCKM、2 = DECKPAM の切り替え、3 = DA・カーソル位置・大きさを端末に尋ねる。Control-C で戻る |
| 8 | 打った文字をそのまま端末に流す（Esc [ 7 m などを手で試せる）。Control-C で戻る |
| 9 | 速さ（300 行を出す時間と、その内訳） |
| 0 | DECALN（画面を E で埋める）、画面全体の反転、カーソルの形 6 種、代わりの画面 |

## ほかのプロジェクトでリンクする

OS がなく、実行のときに読み込むしくみ（動的リンカー）もないので、`.so` ではなく静的ライブラリー（`.a`）にしています。リンクするときに、使う関数だけが取り込まれます。

```make
VT  := vtermlib
HAL := hal
CFLAGS += -I$(VT)/src -I$(VT)/build -I$(HAL)/include -I$(HAL)/se30 -Ilibc/src
stage2.elf: main.o
	$(CC) $(LDFLAGS) -T $(HAL)/se30/stage2.ld -o $@ $(HAL)/build/crt0.o $^ \
	    -L$(VT)/build -L$(HAL)/build -lvterm -lse30 libc/build/libc.a -lgcc
```

- `LDFLAGS` には `-nostdlib -static -no-pie -Wl,--defsym=LOAD_ADDR=0x80000` が要ります（`stage2.ld` が `LOAD_ADDR` を使います）。
- **並べる順**: `-lvterm -lse30 libc/build/libc.a -lgcc`（libvterm は機種のレイヤーの関数を使い、どちらも `memcpy` などを使います）。`libc.a` をパスで渡すわけは `../libc/README.md` にあります。
- **呼ぶ順**: `platform_init()` → `keyboard_init()` → `beep_init()` → `vt_init()`
- `printf`（`../libc`）を端末に出すには、`fmt_putc`（`fmt.h`）を用意し、その中で `vt_putc` を呼びます（`demo/main.c` を参照）。
- 機種のレイヤーの使い方（`trap_fatal` を用意することなど）は `../hal/README.md` にあります。

## プログラムから呼ぶ関数

```c
#include "vt.h"
#include "fmt.h"

vt_init();                              /* platform_init, keyboard_init, beep_init の後 */
printf("\033[1mBold\033[m and \033[7mreverse\033[m\n");
printf("\033#6Double width\n");
int c = vt_getc_wait();                 /* キー。矢印は ESC [ A などのバイト列で来る */
```

| 関数 | 内容 |
|---|---|
| `vt_putc` / `vt_write` / `vt_puts` | 端末へ出す。`printf` も `vt_putc` を通る |
| `vt_set_onlcr(on)` | `'\n'` を `"\r\n"` に直すか（Unix の端末の onlcr と同じ。最初は 1） |
| `vt_flush` | 画面に反映する。出力の途中でも約 1/20 秒ごとに自動で反映する |
| `vt_idle` | 何もしないときに呼ぶ（反映、点滅、キーボードを読む） |
| `vt_getc` / `vt_getc_wait` | 入力（キーと端末の返事）を 1 バイト。なければ -1 / 来るまで待つ |
| `vt_cols` / `vt_rows` | 画面の桁数と行数 |
| `vt_cursor_col` | カーソルの桁（0 から） |
| `vt_redraw` | 画面を全部描き直す（VRAM を直接書き換えた後に） |
| `vt_draw_bitmap(x, y, w, h, bits)` | 1 ビットの絵を重ねる（絵は文字と一緒に流れる。絵のある行に文字を書くと、絵は消える。絵の横に文字を並べるときは文字を先に書く） |

端末からの返事（DA、DSR のカーソル位置など）はキーと同じ流れに入ります。本物の端末と同じく、プログラムは問い合わせを出したあと `vt_getc` で返事を読みます。

## 対応しているシーケンス

**制御文字**: BEL（ベル）、BS、HT、LF / VT / FF（LNM なら行の先頭にも戻る）、CR、SO / SI（G1 / G0）、CAN / SUB（途中のシーケンスを捨てる）、ESC。DEL と 0x80 以上は無視します（7 ビットの端末）。

**ESC**

| シーケンス | 名前 | 内容 |
|---|---|---|
| ESC 7 / ESC 8 | DECSC / DECRC | カーソル、属性、文字の組、DECOM を覚える / 戻す |
| ESC D / E / M | IND / NEL / RI | 1 行下へ / 次の行の先頭へ / 1 行上へ（範囲の端では流す） |
| ESC H | HTS | タブの位置を決める |
| ESC c | RIS | すべて最初の状態へ |
| ESC Z | DECID | ESC [ ? 6 c を返す |
| ESC = / ESC > | DECKPAM / DECKPNM | テンキーをアプリケーション / 数字に |
| ESC ( c / ESC ) c | SCS | G0 / G1 の文字の組。B = ASCII、0 = 線画、A = 英国（# がポンド記号） |
| ESC # 3 / 4 | DECDHL | 倍高の行の上半分 / 下半分（2 行に同じ文字を書く） |
| ESC # 5 / 6 | DECSWL / DECDWL | ふつうの幅 / 倍幅の行（倍幅の行では、行の右半分の文字が見えなくなる） |
| ESC # 8 | DECALN | 画面を E で埋める |
| ESC ] … BEL か ESC \ | OSC | 読み捨てる（窓の題名など）。DCS、SOS、PM、APC も同じ |

**CSI（ESC [）**

| シーケンス | 名前 | 内容 |
|---|---|---|
| CSI n @ / P / X | ICH / DCH / ECH | 文字の挿入 / 削除 / 消去 |
| CSI n A / B / C / D | CUU / CUD / CUF / CUB | カーソルを動かす（上下はスクロール範囲で止まる） |
| CSI n E / F | CNL / CPL | n 行下 / 上の先頭へ |
| CSI n G / \` / a | CHA / HPA / HPR | 桁の指定 / 相対 |
| CSI r ; c H / f | CUP / HVP | 位置の指定（DECOM ならスクロール範囲から数える） |
| CSI n d / e | VPA / VPR | 行の指定 / 相対 |
| CSI n I / Z | CHT / CBT | 次 / 前のタブへ |
| CSI n J / K | ED / EL | 画面 / 行の消去（0 = カーソルから、1 = カーソルまで、2 = 全部）。`CSI ? J/K` も同じ |
| CSI n L / M | IL / DL | 行の挿入 / 削除 |
| CSI n S / T | SU / SD | スクロール範囲を上 / 下へ流す |
| CSI n b | REP | 直前の文字を繰り返す |
| CSI n g | TBC | タブを消す（0 = この位置、3 = 全部） |
| CSI … m | SGR | 0 戻す、1 太字、4 下線、5 / 6 点滅、7 反転、8 見えない、22 / 24 / 25 / 27 / 28 で解除。色（30-49、90-107、38 / 48）は読み飛ばす |
| CSI t ; b r | DECSTBM | スクロール範囲（カーソルは左上へ） |
| CSI 4 h / l | IRM | 挿入モード |
| CSI 20 h / l | LNM | LF で行の先頭にも戻る。Return で CR LF を送る |
| CSI ? n h / l | DECSET | 1 DECCKM、4 DECSCLM（スムーズスクロール）、5 DECSCNM（画面の反転）、6 DECOM、7 DECAWM、12 カーソルの点滅、25 DECTCEM、47 / 1047 / 1049 代わりの画面、1048 カーソルを覚える |
| CSI c / CSI > c | DA / DA2 | ESC [ ? 6 c（VT102）/ ESC [ > 1 ; 10 ; 0 c |
| CSI 5 n / 6 n | DSR | ESC [ 0 n / カーソル位置 ESC [ 行 ; 桁 R。`CSI ? 6 n` は ESC [ ? 行 ; 桁 R |
| CSI 18 t | | 大きさを ESC [ 8 ; 28 ; 85 t で返す |
| CSI s / u | SCOSC / SCORC | ESC 7 / ESC 8 と同じ |
| CSI n SP q | DECSCUSR | カーソルの形（0 / 1 点滅ブロック、2 ブロック、3 / 4 下線、5 / 6 縦線） |
| CSI ! p | DECSTR | モードを最初の状態へ（画面はそのまま） |
| CSI x | DECREQTPARM | 端末の設定を返す |

**していないこと**

- 132 桁（DECCOLM は無視）
- 色
- 8 ビットの文字と C1 制御文字、UTF-8
- 文字の保護（DECSCA）
- プリンター
- VT52 モード
- 画面より前の行を残すこと（スクロールバック）

## キーが送るもの

| キー | 送るもの |
|---|---|
| 文字のキー | ASCII。Control + 英字は 1-26、Control + [ は ESC。Delete（後退）は DEL（0x7F） |
| Return | CR（LNM なら CR LF） |
| 矢印 | ESC [ A-D（DECCKM なら ESC O A-D） |
| Home / End | ESC [ H / F（DECCKM なら ESC O H / F） |
| Help（拡張キーボードの Insert の位置）/ Del / Page Up / Page Down | ESC [ 2 ~ / 3 ~ / 5 ~ / 6 ~ |
| F1-F4、テンキーの Clear | ESC O P-S（Clear は PF1） |
| F5-F12 | ESC [ 15 ~、17 ~、18 ~、19 ~、20 ~、21 ~、23 ~、24 ~ |
| テンキーの = / \* | PF2-PF4（ESC O Q / R / S。VT100 のテンキーには = と * がないので、PF キーに当てる） |
| テンキーの数字、-、+、.、Enter | ふだんはその文字。DECKPAM なら ESC O p-y、m、l（+ は VT100 のコンマの位置）、n、M |

## フォント

`assets/font/vt6x12.txt`（2026-11-18 に新しく描いたもの）です。ファイルの先頭に書き方があります。

```
size 6 12          ; 1 文字のます目 (幅 1-8, 高さ)。桁数 = 512 / 幅、行数 = 342 / 高さ
underline 11       ; 下線を引く行
[A]                ; 次の 12 行が 'A'
.###..
...
{q}                ; 線画文字 (ESC ( 0 のあとの q = 横線)
```

- 英数字は 1〜5 列目に描き、6 列目は文字の間のすきまにします（太字はここへ 1 ドット太ります）。大文字は 3〜10 行目、下がる部分は 11〜12 行目です。
- 線画文字はます目いっぱいに描き、縦線は 3 列目、横線は 7 行目を通します（隣の文字とつながります）。
- `tools/common/vtfont.py` が `build/font.h` にします。書いてない文字は空白、書いてない線画文字は ASCII の形になります。

## しくみ

```
program  -- vt_putc -->  vtcore.c  -->  vtdraw.c  -->  shadow (RAM)  -->  VRAM
program  <-- vt_getc --  vtcore.c
keyboard -- keyboard_getc -->  vtcore_key

vtcore.c      文字の流れを解釈し、文字の表と行の印 (変わった行) を持つ
vtdraw.c      変わった行を shadow に組み立てる
VRAM          shadow を写すときに、カーソルと画面全体の反転を重ねる
vtcore_key    キーをバイト列 (ESC [ A など) にする
```

| ファイル | 内容 |
|---|---|
| `src/vtcore.c` | 端末の中身。文字の流れを 1 バイトずつ解釈するステートマシンと、文字の表。画面には触らない（PC でも動く） |
| `src/vtdraw.c` | 文字の表を画面にする |
| `src/vt.c` | インターフェース（`vt.h`）。中身、描く側、キーボード、ベルをつなぐ |
| `demo/main.c` | デモ |
| `tests/test_vt.c` | PC で中身を確認する（`make test`） |
| `tools/common/vtfont.py` | テキストのフォントを `build/font.h` にする |

- **文字の表**は画面 2 枚分（通常と代わりの画面）です。行は番号の表（map）で引くので、スクロールは番号を回すだけで済みます。
- **画面の組み立て**: 画面と同じ形の絵（shadow）を RAM に持ちます。変わった行だけを組み立て、変わった行だけを VRAM に写します。VRAM は読むのが遅いので、スクロールも shadow の中で動かしてから写します。
- **ふつうの幅の行**は、ドットの 1 行ずつ「16 文字 = 3 語」（6 ドット幅のとき）を決まったシフトで詰めます。シフトの量はフォントの幅から決まる定数なので、コンパイラーが分岐のない命令の並びに展開します（1 文字・1 行あたり 5 命令ほど）。倍幅・倍高の行は 1 文字ずつ置きます。
- **ジャンプスクロール**は行数を数えておき、反映のときにまとめて動かします。何行流れても、VRAM に写すのは 1 回の反映に 1 回です。
- **スムーズスクロール**は垂直同期ごとに 2 ドットずつ動かして写します（12 ドットの行で 1 行 6 回 = 実機で約 10 行/秒）。終わるまで戻らないので、出力も遅くなります（本物の VT100 と同じです）。
- 表示できる ASCII を行の途中に書く場合は、近道（`vtcore_putc` の最初）で済ませます。

## ソースの決まり

名前の付け方とコメントの書き方は、woshell の `README.md` の「ソースの決まり」に従います。

## 速さ（MAME、2026-11-18）

デモの 9 の結果です。

| 項目 | 速さ |
|---|---|
| printf から画面まで（80 文字の行、ジャンプスクロール） | 120 行/秒（9700 文字/秒） |
| 中身（`vtcore.c`）だけ（1 文字あたり） | 22µs |
| 1 行の組み立て | 4.9ms |
| 画面全体を VRAM に写す | 3.5ms |

最初の版は 53 行/秒でした（1 行の組み立て 10.9ms、画面全体を VRAM に写す時間 17.9ms）。次の 3 つを変えて、今の速さになりました。

- 組み立てを、定数のシフトで詰める形にした
- VRAM への書き込みを、1 行 16 個の move.l にした
- 中身（`vtcore_putc`）に近道を作った

実機ではまだ測っていません。

## 確認したこと

- `make test`: 77 項目すべて通ります（折り返し、DECAWM、カーソル移動、消去、挿入と削除、スクロール範囲、DECOM、タブ、属性、文字の組、倍幅、DECALN、代わりの画面、DECSC、文字列の読み捨て、CAN、REP、ベル、キー、返事、RIS）。
- MAME: デモの全項目をスクリーンショットで確認しました（キーの画面は打ったキーと、DA・カーソル位置・大きさの返事まで）。
- **実機ではまだ動かしていません**（ベルの音、スムーズスクロールの見え方、速さ）。

## 次にできること

- 実機で確認する（デモの 9 の速さ、スムーズスクロール、ベル）
- シリアルにつないで本物の端末として使う
- 組み立てをさらに速くする（左の余白の分のずらしを、詰める処理の中で一緒に行う、など）
