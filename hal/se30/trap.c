/**
 * @file trap.c
 * @brief 例外の処理 (trap.h, trapent.S)
 *
 * 68030 の例外のフレーム: +0 SR (2 バイト), +2 PC (4), +6 形式 (上位 4 ビット) とベクタの位置 (下位 12 ビット)。
 * 形式 $A / $B (バスエラー, アドレスエラー) は、+$0A の SSW (特別な状態の語) で、どのアクセスで起きたかがわかる。
 *   DF (ビット 8): データのアクセス。触ろうとしたアドレスは +$10 にある
 *   FB / FC (ビット 14 / 15): 命令の読み込み (パイプラインのステージ B / C)。ステージ B のアドレスは、
 *       形式 $B なら +$24 にあり、形式 $A なら PC + 4。ステージ C はステージ B の 2 バイト前
 * どれも立っていなければ、触ろうとしたアドレスはフレームに残っていない (+$10 の値は意味がない)。
 */
#include "trap.h"

extern volatile uint32_t trap_armed;            /* trapent.S */
extern struct trap_info *volatile trap_infop;   /* trapent.S */
void trap_resume(int vector) __attribute__((noreturn));
int trap_exc_handler(const uint8_t *frame);

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) << 16 | rd16(p + 2); }

int trap_exc_handler(const uint8_t *frame)
{
    const uint16_t fv = rd16(frame + 6);
    const int format = fv >> 12, vector = (fv & 0xFFF) >> 2;
    struct trap_info info;
    info.vector = vector;
    info.sr = rd16(frame);
    info.pc = rd32(frame + 2);
    info.has_addr = 0;
    info.addr = 0;
    if (format == 0xA || format == 0xB) {
        const uint16_t ssw = rd16(frame + 0x0A);
        if (ssw & 0x0100) {                         /* DF: データのアクセス */
            info.has_addr = 1;
            info.addr = rd32(frame + 0x10);
        } else if (ssw & 0xC000) {                  /* FC / FB: 命令の読み込み */
            const uint32_t stage_b = format == 0xB ? rd32(frame + 0x24) : info.pc + 4;
            info.has_addr = 1;
            info.addr = (ssw & 0x8000) ? stage_b - 2 : stage_b;   /* C のほうが先の命令 */
        }
    }

    if (!trap_armed) {
        if (vector == 31)       /* 構えていないときの割り込みボタンは無視する */
            return 1;
        trap_fatal(&info);
        return 0;
    }
    *trap_infop = info;
    trap_resume(vector);
}

const char *trap_name(int vector)
{
    switch (vector) {
    case 2:  return "bus error";
    case 3:  return "address error";
    case 4:  return "illegal instruction";
    case 5:  return "zero divide";
    case 6:  return "chk";
    case 7:  return "trapv";
    case 8:  return "privilege violation";
    case 9:  return "trace";
    case 10: return "line 1010 emulator";
    case 11: return "line 1111 emulator";
    case 13: return "coprocessor protocol violation";
    case 14: return "format error";
    case 15: return "uninitialized interrupt";
    case 24: return "spurious interrupt";
    case 31: return "interrupt button (NMI)";
    case 48: case 49: case 50: case 51: case 52: case 53: case 54:
        return "floating point exception";
    case 56: case 57: case 58:
        return "MMU exception";
    default:
        if (vector >= 32 && vector <= 47)
            return "trap";
        return "exception";
    }
}
