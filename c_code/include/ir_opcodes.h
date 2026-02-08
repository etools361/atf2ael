/*
 * ir_opcodes.h
 * AEL to IR Compiler - IR Opcode Definitions
 */

#ifndef IR_OPCODES_H
#define IR_OPCODES_H

/* IR操作码定义 */
typedef enum {
    OP_STMT_END = 0,        /* 语句结束 */
    OP_LOAD_INT = 3,        /* 加载整数常量 */
    OP_LOAD_STR = 4,        /* 加载字符串常量 */
    OP_LOAD_REAL = 8,       /* 加载实数常量 */
    OP_LOAD_IMAG = 9,       /* 加载虚数常量 */
    OP_LOAD_NULL = 10,      /* 加载NULL值 */
    OP_LOAD_VAR = 16,       /* 加载变量/函数名 */
    OP_ADD_LOCAL = 20,      /* 添加局部变量 */
    OP_BEGIN_FUNCT = 32,    /* 开始函数定义 */
    OP_DEFINE_FUNCT = 33,   /* 完成函数定义 */
    OP_BRANCH_TRUE = 34,    /* 条件分支（为真时跳转） */
    OP_SET_LABEL = 42,      /* 设置标签位置 */
    OP_ADD_LABEL = 43,      /* 添加标签 */
    OP_ADD_GLOBAL = 44,     /* 添加全局变量 */
    OP_OP = 48,             /* 通用操作（arg1指定具体操作，别名OP_GENERIC） */
    OP_GENERIC = 48,        /* 通用操作（arg1指定具体操作） */
    OP_NUM_LOCAL = 52,      /* 声明局部变量数量 */
    OP_DROP_LOCAL = 55      /* 清除局部变量 */
} IROpcode;

/* OP_GENERIC的子操作码（arg1字段） */
typedef enum {
    SUBOP_STMT_END = 0,     /* 语句结束 */
    SUBOP_NEGATE = 15,      /* 取负 */
    SUBOP_ASSIGN = 16,      /* 赋值操作 */
    SUBOP_EXPR = 17,        /* 表达式求值 */
    SUBOP_RETURN = 20,      /* 返回语句 */
    SUBOP_CALL = 48,        /* 函数调用 */
    SUBOP_PUSH_ARGS = 56    /* 压栈参数 */
} IRSubOpcode;

#endif /* IR_OPCODES_H */
