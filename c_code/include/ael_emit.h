#pragma once

#include <stdbool.h>
#include <stdio.h>

typedef struct AelEmitter {
    FILE *fp;
    int line0;
    int col0;
    bool strict_pos;
    bool allow_num_local_scope_blocks;

    /* Diagnostics (best-effort). */
    int last_req_line0;
    int last_req_col0;
    int last_fail_reason;
} AelEmitter;

bool ael_emit_init(AelEmitter *e, FILE *fp, bool strict_pos);
bool ael_emit_at(AelEmitter *e, int line0, int col0);
bool ael_emit_text(AelEmitter *e, const char *text);
bool ael_emit_char(AelEmitter *e, char ch);

enum {
    AEL_EMIT_FAIL_NONE = 0,
    AEL_EMIT_FAIL_BACKWARD_LINE = 1,
    AEL_EMIT_FAIL_BACKWARD_COL = 2,
    AEL_EMIT_FAIL_IO = 3
};
