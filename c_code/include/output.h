/*
 * output.h
 * AEL to IR Compiler - Output Module Header
 */

#ifndef OUTPUT_H
#define OUTPUT_H

#include "ir_types.h"

/* 输出IR文件 (包含所有信息：控制流、符号表、嵌套深度) */
bool output_ir_file(const char *output_base, IRContext *ctx);

#endif /* OUTPUT_H */
