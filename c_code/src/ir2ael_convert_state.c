/* ir2ael_convert_state.c - conversion state helpers */
#include "ir2ael_internal.h"
#include <stdlib.h>
#include <string.h>

void ir2ael_state_init(Ir2AelState *s, const IRProgram *program, AelEmitter *out, char *err, size_t err_cap) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->program = program;
    s->out = out;
    s->err = err;
    s->err_cap = err_cap;
    s->current_defun_line0 = -1;
    s->pending_defun_name[0] = '\0';
    s->pending_inline_else_line0 = -1;
    s->pending_inline_else_col0 = -1;
    s->sw.table_label = -1;
    s->sw.last_break_line0 = -1;
    decl_group_clear(&s->pending_decls);
    local_init_clear(&s->local_init);
}

void ir2ael_state_free(Ir2AelState *s) {
    if (!s) return;
    stack_clear(s->stack, &s->stack_len);
    free(s->stack);
    s->stack = NULL;
    s->stack_len = 0;
    s->stack_cap = 0;
}
