/*
 * output.c
 * AEL to IR Compiler - Output Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "output.h"
#include "ir_opcodes.h"

/* 转义字符串用于IR输出
 * 将字符串中的特殊字符转换为转义序列，以便在IR文件中正确显示
 * 转义规则：
 *   \n -> \\n (换行符 -> 反斜杠+n)
 *   \t -> \\t (制表符 -> 反斜杠+t)
 *   \r -> \\r (回车符 -> 反斜杠+r)
 *   \" -> \\\" (双引号 -> 反斜杠+引号)
 *   \\ -> \\\\ (反斜杠 -> 双反斜杠)
 */
static char* escape_string_for_ir_output(const char *str) {
    if (!str) return NULL;

    /* 计算转义后字符串的最大长度（每个字符最多变成2个字符）*/
    size_t max_len = strlen(str) * 2 + 1;
    char *escaped = (char*)malloc(max_len);
    if (!escaped) return NULL;

    size_t j = 0;
    for (size_t i = 0; str[i]; i++) {
        switch (str[i]) {
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            default:
                escaped[j++] = str[i];
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

/* 获取操作码的注释名称 */
static const char* get_opcode_name(uint8_t opcode) {
    switch (opcode) {
        case OP_STMT_END: return "STMT_END";
        case OP_LOAD_INT: return "LOAD_INT";
        case OP_LOAD_STR: return "LOAD_STR";
        case OP_LOAD_REAL: return "LOAD_REAL";
        case OP_LOAD_IMAG: return "LOAD_IMAG";
        case OP_LOAD_NULL: return "LOAD_NULL";
        case OP_LOAD_VAR: return "LOAD_VAR";
        case OP_ADD_LOCAL: return "ADD_LOCAL";
        case OP_BEGIN_FUNCT: return "BEGIN_FUNCT";
        case OP_DEFINE_FUNCT: return "DEFINE_FUNCT";
        case OP_BRANCH_TRUE: return "BRANCH_TRUE";
        case OP_SET_LABEL: return "SET_LABEL";
        case OP_ADD_LABEL: return "ADD_LABEL";
        case OP_ADD_GLOBAL: return "ADD_GLOBAL";
        case 36: return "BEGIN_LOOP";
        case 37: return "END_LOOP";
        case 38: return "LOOP_AGAIN";
        case 39: return "LOOP_EXIT";
        case 40: return "ADD_CASE";
        case 41: return "BRANCH_TABLE";
        case OP_GENERIC: return "OP";
        case OP_NUM_LOCAL: return "NUM_LOCAL";
        case 53: return "SET_LOOP_DEFAULT";
        case OP_DROP_LOCAL: return "DROP_LOCAL";
        default: return "UNKNOWN";
    }
}

/* 获取调用函数名称 */
static const char* get_function_name(uint8_t opcode) {
    switch (opcode) {
        case OP_NUM_LOCAL: return "acomp_num_local";
        case OP_ADD_LOCAL: return "acomp_add_local";
        case OP_ADD_GLOBAL: return "acomp_add_global";
        case OP_DROP_LOCAL: return "acomp_drop_local";
        case OP_LOAD_VAR: return "acomp_word_ref";
        case OP_LOAD_STR: return "acomp_string";
        case OP_LOAD_INT: return "acomp_integer";
        case OP_LOAD_REAL: return "acomp_real";
        case OP_LOAD_IMAG: return "acomp_imag";
        case OP_LOAD_NULL: return "acomp_null";
        case OP_BEGIN_FUNCT: return "acomp_begin_funct";
        case OP_DEFINE_FUNCT: return "acomp_define_funct";
        case OP_BRANCH_TRUE: return "acomp_branch_true";
        case OP_SET_LABEL: return "acomp_set_label";
        case OP_ADD_LABEL: return "acomp_add_label";
        case 36: return "acomp_begin_loop";
        case 37: return "acomp_end_loop";
        case 38: return "acomp_loop_again";
        case 39: return "acomp_loop_exit";
        case 40: return "acomp_add_case";
        case 41: return "acomp_branch_table";
        case 53: return "acomp_set_loop_default";
        case OP_GENERIC: return "acomp_op";
        default: return NULL;
    }
}

/* 输出IR文件 */
bool output_ir_file(const char *output_base, IRContext *ctx) {
    char filename[512];
    snprintf(filename, sizeof(filename), "%s.ir.txt", output_base);

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create IR file: %s\n", filename);
        return false;
    }

    /* 输出文件头注释 */
    fprintf(fp, "# AEL IR Log from Pure C Compiler\n");
    fprintf(fp, "# Source: %s\n", ctx->source_file ? ctx->source_file : "unknown");

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "# Generated: %04d-%02d-%02d %02d:%02d:%02d\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

    fprintf(fp, "# Method: Pure C Implementation\n");
    fprintf(fp, "#\n\n");

    /* 输出每条IR指令 */
    for (int i = 0; i < ctx->instruction_count; i++) {
        IRInstruction *inst = &ctx->instructions[i];

        /* 指令行 */
        fprintf(fp, "[%04X] OP=%3d  ", inst->address, inst->opcode);

        /* 字符串参数 */
        if (inst->has_str && inst->str_arg) {
            fprintf(fp, "str=\"%s\"  ", inst->str_arg);
        }

        /* 数值参数 */
        if (inst->has_args) {
            fprintf(fp, "arg1=%5d  arg2=%5d  arg3=%5d  ",
                    inst->arg1, inst->arg2, inst->arg3);
            if (inst->arg4 != 0) {
                fprintf(fp, "a4=%d  ", inst->arg4);
            }
        }

        /* 注释 */
        const char *op_name = get_opcode_name(inst->opcode);
        const char *func_name = get_function_name(inst->opcode);

        fprintf(fp, "# %s", op_name);

        /* 特殊处理：DROP_LOCAL显示count参数 */
        if (inst->opcode == OP_DROP_LOCAL) {
            fprintf(fp, " count=%d", inst->arg1);
        }
        /* 特殊处理：BRANCH_TRUE显示label参数 */
        else if (inst->opcode == OP_BRANCH_TRUE) {
            fprintf(fp, " label=%d", inst->arg1);
        }
        /* 特殊处理：SET_LABEL显示label_id参数 */
        else if (inst->opcode == OP_SET_LABEL) {
            fprintf(fp, " label_id=%d", inst->arg1);
        }
        /* 特殊处理：ADD_CASE显示case_value参数 */
        else if (inst->opcode == 40) {
            fprintf(fp, " case_value=%d", inst->arg1);
        }
        /* 特殊处理：BRANCH_TABLE显示line和col参数 */
        else if (inst->opcode == 41) {
            fprintf(fp, " line=%d col=%d", inst->arg1, inst->arg2);
        }

        if (func_name) {
            fprintf(fp, " (%s)", func_name);
        }

        fprintf(fp, "\n");

        /* DEPTH标记 */
        if (inst->depth > 0) {
            fprintf(fp, "    # DEPTH=%d\n", inst->depth);
        }
    }

    fprintf(fp, "\n# End of IR log (Total: %d instructions)\n", ctx->instruction_count);

    fclose(fp);
    return true;
}
