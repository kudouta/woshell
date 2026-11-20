/**
 * @file keyboard.h
 * @brief キーボード (機種に依存しない入口)
 *
 * 文字のキーは ASCII で返す (Shift, Caps Lock, Control を反映。Control + 英字は 1-26)。
 * Return は '\\r'、Delete (後退) は 0x7F、Tab は '\\t'、Esc は 0x1B。
 * テンキーは KEYBOARD_KP | 文字 ('0'-'9', '.', '+', '-', '*', '/', '=', '\\r') で返す
 * (端末がアプリケーションキーパッドのときに、ほかのキーと見分けるため)。
 * 矢印などの文字でないキーは KEYBOARD_ の値 (0x100 以上) で返す。
 * 押し続けると、少し待ってから繰り返す (ADB のキーボードは自分では繰り返さないので、ソフトで作る)。
 */
#ifndef INCLUDE_KEYBOARD_H
#define INCLUDE_KEYBOARD_H

/** 文字でないキーの値 */
enum {
    KEYBOARD_UP = 0x100, KEYBOARD_DOWN, KEYBOARD_LEFT, KEYBOARD_RIGHT,
    KEYBOARD_HOME, KEYBOARD_END, KEYBOARD_PGUP, KEYBOARD_PGDN,
    KEYBOARD_DEL,                       /**< 前へ消す Del (Delete = 後退は 0x7F) */
    KEYBOARD_HELP,                      /**< 拡張キーボードの Help (Insert の位置) */
    KEYBOARD_CLEAR,                     /**< テンキーの Clear */
    KEYBOARD_F1, KEYBOARD_F2, KEYBOARD_F3, KEYBOARD_F4, KEYBOARD_F5, KEYBOARD_F6,
    KEYBOARD_F7, KEYBOARD_F8, KEYBOARD_F9, KEYBOARD_F10, KEYBOARD_F11, KEYBOARD_F12,
};

/** テンキーの印。テンキーは KEYBOARD_KP | 文字 で返す */
#define KEYBOARD_KP (0x200)

/**
 * @brief キーボードを使えるようにする
 *
 * platform_init の後に呼ぶ。
 */
void keyboard_init(void);

/**
 * @brief 押されたキーを 1 つ取り出す (待たない)
 * @return 文字 (ASCII)、KEYBOARD_ の値、KEYBOARD_KP | 文字 のどれか。なければ -1
 */
int keyboard_getc(void);

#endif
