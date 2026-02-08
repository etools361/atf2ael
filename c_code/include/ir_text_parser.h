#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct IRInst {
    int index; /* -1 if absent */
    int op;    /* OP= */

    /* Optional DEPTH metadata from IR logs (lexical nesting level). */
    bool has_depth;
    int depth;

    bool has_arg1;
    bool has_arg2;
    bool has_arg3;
    bool has_a4;

    int arg1;
    int arg2;
    int arg3;
    int a4;

    char *str; /* optional, heap allocated */

    /* For OP=8/OP=9 values (parsed from inline comment "val="). */
    bool has_num_val;
    double num_val;
} IRInst;

typedef struct IRProgram {
    IRInst *insts;
    size_t count;
    size_t cap;
} IRProgram;

bool ir_program_init(IRProgram *p);
void ir_program_free(IRProgram *p);

/* Parses an AEL IR log file (*.ir.txt). Ignores comment and DEPTH lines. */
bool ir_parse_file(const char *path, IRProgram *out_program, char *err, size_t err_cap);

/*
 * Extracts the "# Source: <path>" header value from an IR log.
 * Returns true if present (path copied into out_path).
 */
bool ir_extract_source_ael_path(const char *ir_path, char *out_path, size_t out_cap);
