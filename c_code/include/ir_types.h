/*
 * ir_types.h
 * AEL to IR Compiler - Data Type Definitions
 */

#ifndef IR_TYPES_H
#define IR_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* IR指令结构 */
typedef struct {
    uint16_t address;          /* 指令地址 */
    uint8_t opcode;            /* 操作码 */
    char *str_arg;             /* 字符串参数（可选） */
    int16_t arg1;              /* 参数1 */
    int16_t arg2;              /* 参数2 */
    int16_t arg3;              /* 参数3 */
    int16_t arg4;              /* 参数4（a4字段） */
    double real_val;           /* 实数值（用于OP_LOAD_REAL） */
    uint8_t depth;             /* 嵌套深度 */
    bool has_str;              /* 是否有字符串参数 */
    bool has_args;             /* 是否有数值参数 */
} IRInstruction;

/* 符号类型 */
typedef enum {
    SYM_GLOBAL,               /* 全局变量 */
    SYM_LOCAL,                /* 局部变量 */
    SYM_FUNCTION,             /* 函数 */
    SYM_ARG                   /* 函数参数 */
} SymbolType;

/* 符号表项 */
typedef struct {
    SymbolType type;          /* 符号类型 */
    char *name;               /* 符号名称 */
    uint16_t offset;          /* 偏移/地址 */
} Symbol;

/* IR代码生成器上下文 */
typedef struct {
    IRInstruction *instructions;   /* IR指令数组 */
    int instruction_count;          /* 指令数量 */
    int instruction_capacity;       /* 指令容量 */

    Symbol *symbols;                /* 符号表 */
    int symbol_count;               /* 符号数量 */
    int symbol_capacity;            /* 符号容量 */

    uint8_t current_depth;          /* 当前嵌套深度 */
    char *source_file;              /* 源文件路径 */
} IRContext;

#endif /* IR_TYPES_H */
