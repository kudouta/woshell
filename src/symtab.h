/**
 * @file symtab.h
 * @brief プログラムの記号の表 (関数や変数の名前と番地)
 *
 * build/symtab.c は、リンクした結果 (nm) から tools/common/mksymtab.py が作る。
 * 表の大きさで番地がずれないよう、Makefile で 3 回リンクする (README の「しくみ」)。
 * シェルのコマンドの関数 (cmd_help など) は、cmd_ を外した名前 (help) でも入っている (型 'C')。
 */
#ifndef SYMTAB_H
#define SYMTAB_H

#include <stdint.h>

/** 記号の表の 1 項目 */
struct symtab_entry {
    const char *name;                   /**< 名前 */
    uint32_t addr;                      /**< 番地 */
    /** 種類: 'T' = 関数 (命令), 'C' = シェルのコマンドの短い名前 (関数), 'D' = 初期値のある変数,
        'B' = 初期値のない変数, 'R' = 読み出し専用 */
    char type;
};

/** 記号の表 (名前の順。最後は name が 0 の項目) */
extern const struct symtab_entry symtab_table[];
/** 記号の表の項目の数 (最後の 0 の項目は数えない) */
extern const int symtab_count;

/**
 * @brief 記号の種類が関数かどうか
 * @param type struct symtab_entry の type
 * @return 関数 ('T' か 'C') なら 1
 */
static inline int symtab_is_func(char type)
{
    return type == 'T' || type == 'C';
}

#endif
