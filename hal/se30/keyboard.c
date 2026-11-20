/**
 * @file keyboard.c
 * @brief ADB のキーコードを文字にする (keyboard.h)。US 配列
 *
 * ADB のキーボードは、押したときと離したときにキーコード (7 ビット) を送ってくる (adb.c)。
 * Caps Lock は機械的に固定されるキーで、固定したときに「押した」、外したときに「離した」が来る。
 * キーの繰り返しは ADB のキーボードにはないので、ここで時計を見て作る。
 */
#include "keyboard.h"
#include "timer.h"
#include "adb.h"

/* ADB のキーコード -> 文字 (Shift なし / あり)。0 = 文字にならないキー */
static const char plain[0x80] = {
    [0x00] = 'a', [0x01] = 's', [0x02] = 'd', [0x03] = 'f', [0x04] = 'h', [0x05] = 'g',
    [0x06] = 'z', [0x07] = 'x', [0x08] = 'c', [0x09] = 'v', [0x0B] = 'b', [0x0C] = 'q',
    [0x0D] = 'w', [0x0E] = 'e', [0x0F] = 'r', [0x10] = 'y', [0x11] = 't', [0x12] = '1',
    [0x13] = '2', [0x14] = '3', [0x15] = '4', [0x16] = '6', [0x17] = '5', [0x18] = '=',
    [0x19] = '9', [0x1A] = '7', [0x1B] = '-', [0x1C] = '8', [0x1D] = '0', [0x1E] = ']',
    [0x1F] = 'o', [0x20] = 'u', [0x21] = '[', [0x22] = 'i', [0x23] = 'p', [0x24] = '\r',
    [0x25] = 'l', [0x26] = 'j', [0x27] = '\'', [0x28] = 'k', [0x29] = ';', [0x2A] = '\\',
    [0x2B] = ',', [0x2C] = '/', [0x2D] = 'n', [0x2E] = 'm', [0x2F] = '.', [0x30] = '\t',
    [0x31] = ' ', [0x32] = '`', [0x33] = 0x7F, [0x35] = 0x1B,
    /* テンキー */
    [0x41] = '.', [0x43] = '*', [0x45] = '+', [0x4B] = '/', [0x4C] = '\r', [0x4E] = '-',
    [0x51] = '=', [0x52] = '0', [0x53] = '1', [0x54] = '2', [0x55] = '3', [0x56] = '4',
    [0x57] = '5', [0x58] = '6', [0x59] = '7', [0x5B] = '8', [0x5C] = '9',
};

static const char shifted[0x80] = {
    [0x00] = 'A', [0x01] = 'S', [0x02] = 'D', [0x03] = 'F', [0x04] = 'H', [0x05] = 'G',
    [0x06] = 'Z', [0x07] = 'X', [0x08] = 'C', [0x09] = 'V', [0x0B] = 'B', [0x0C] = 'Q',
    [0x0D] = 'W', [0x0E] = 'E', [0x0F] = 'R', [0x10] = 'Y', [0x11] = 'T', [0x12] = '!',
    [0x13] = '@', [0x14] = '#', [0x15] = '$', [0x16] = '^', [0x17] = '%', [0x18] = '+',
    [0x19] = '(', [0x1A] = '&', [0x1B] = '_', [0x1C] = '*', [0x1D] = ')', [0x1E] = '}',
    [0x1F] = 'O', [0x20] = 'U', [0x21] = '{', [0x22] = 'I', [0x23] = 'P', [0x24] = '\r',
    [0x25] = 'L', [0x26] = 'J', [0x27] = '"', [0x28] = 'K', [0x29] = ':', [0x2A] = '|',
    [0x2B] = '<', [0x2C] = '?', [0x2D] = 'N', [0x2E] = 'M', [0x2F] = '>', [0x30] = '\t',
    [0x31] = ' ', [0x32] = '~', [0x33] = 0x7F, [0x35] = 0x1B,
    [0x41] = '.', [0x43] = '*', [0x45] = '+', [0x4B] = '/', [0x4C] = '\r', [0x4E] = '-',
    [0x51] = '=', [0x52] = '0', [0x53] = '1', [0x54] = '2', [0x55] = '3', [0x56] = '4',
    [0x57] = '5', [0x58] = '6', [0x59] = '7', [0x5B] = '8', [0x5C] = '9',
};

/* 修飾キー */
#define K_CONTROL (0x36)
#define K_SHIFT   (0x38)
#define K_CAPS    (0x39)
#define K_OPTION  (0x3A)
#define K_COMMAND (0x37)
#define K_RSHIFT  (0x7B)            /* 右の Shift (左右を分けて送る設定のとき) */
#define K_RCTRL   (0x7D)

/* 文字でないキー */
static int special(int code)
{
    switch (code) {
    case 0x3B: return KEYBOARD_LEFT;
    case 0x3C: return KEYBOARD_RIGHT;
    case 0x3D: return KEYBOARD_DOWN;
    case 0x3E: return KEYBOARD_UP;
    case 0x73: return KEYBOARD_HOME;
    case 0x77: return KEYBOARD_END;
    case 0x74: return KEYBOARD_PGUP;
    case 0x79: return KEYBOARD_PGDN;
    case 0x75: return KEYBOARD_DEL;
    case 0x72: return KEYBOARD_HELP;
    case 0x47: return KEYBOARD_CLEAR;
    case 0x7A: return KEYBOARD_F1;
    case 0x78: return KEYBOARD_F2;
    case 0x63: return KEYBOARD_F3;
    case 0x76: return KEYBOARD_F4;
    case 0x60: return KEYBOARD_F5;
    case 0x61: return KEYBOARD_F6;
    case 0x62: return KEYBOARD_F7;
    case 0x64: return KEYBOARD_F8;
    case 0x65: return KEYBOARD_F9;
    case 0x6D: return KEYBOARD_F10;
    case 0x67: return KEYBOARD_F11;
    case 0x6F: return KEYBOARD_F12;
    default:   return -1;
    }
}

#define REPEAT_DELAY (TIMER_HZ / 2)      /* 押してから繰り返し始めるまで (0.5 秒) */
#define REPEAT_RATE  (TIMER_HZ / 20)     /* 繰り返しの間隔 (1 秒に 20 回) */

static int shift, control, caps;
static int held = -1;                   /* 押し続けているキー (繰り返す) */
static uint32_t held_next;              /* 次に繰り返す時刻 */

void keyboard_init(void)
{
    adb_start();
}

/* キーコードを文字にする (-1 = 文字にならない) */
static int translate(int code)
{
    int s = special(code);
    if (s >= 0)
        return s;
    int c = (shift ? shifted : plain)[code];
    if (!c)
        return -1;
    if (code >= 0x41 && code <= 0x5C)   /* テンキー */
        return KEYBOARD_KP | c;
    if (caps && c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    else if (caps && shift && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    if (control) {
        if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z')
            c = (c | 0x20) - 'a' + 1;   /* Control + 英字 = 1-26 */
        else if (c == '[')
            c = 0x1B;
    }
    return c;
}

int keyboard_getc(void)
{
    adb_watchdog();
    for (int ev; (ev = adb_get_event()) >= 0; ) {
        const int code = ev & 0x7F, up = ev & ADB_KEY_UP_FLAG;
        switch (code) {
        case K_SHIFT: case K_RSHIFT:
            shift = !up;
            continue;
        case K_CONTROL: case K_RCTRL:
            control = !up;
            continue;
        case K_CAPS:
            caps = !up;
            continue;
        case K_OPTION: case K_COMMAND:
            continue;
        }
        if (up) {
            if (code == held)
                held = -1;
            continue;
        }
        const int c = translate(code);
        if (c < 0)
            continue;
        held = code;
        held_next = timer_now() + REPEAT_DELAY;
        return c;
    }
    /* 押し続けているキーの繰り返し */
    if (held >= 0 && (int32_t)(timer_now() - held_next) >= 0) {
        held_next = timer_now() + REPEAT_RATE;   /* 長く呼ばれなかったときに、まとめて繰り返さない */
        return translate(held);
    }
    return -1;
}
