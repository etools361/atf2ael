/* ir2ael_convert_load.c - literal and variable load handling */
#include "ir2ael_internal.h"
#include <string.h>

Ir2AelStatus ir2ael_handle_load_ops(Ir2AelState *s, const IRInst *inst) {
    if (!s || !inst) return IR2AEL_STATUS_FAIL;

    if (inst->op == OP_LOAD_INT) {
        Expr *e = expr_new(EXPR_INT);
        if (!e) return IR2AEL_STATUS_OOM;
        e->int_value = inst->has_arg1 ? inst->arg1 : 0;
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }
    if (inst->op == 5) { /* LOAD_BOOL */
        Expr *e = expr_new(EXPR_BOOL);
        if (!e) return IR2AEL_STATUS_OOM;
        e->bool_value = inst->has_arg1 ? (inst->arg1 != 0) : 0;
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }
    if (inst->op == OP_LOAD_REAL) {
        Expr *e = expr_new(EXPR_REAL);
        if (!e) return IR2AEL_STATUS_OOM;
        e->num_value = inst->has_num_val ? inst->num_val : 0.0;
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }
    if (inst->op == OP_LOAD_IMAG) {
        Expr *e = expr_new(EXPR_IMAG);
        if (!e) return IR2AEL_STATUS_OOM;
        e->num_value = inst->has_num_val ? inst->num_val : 0.0;
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }
    if (inst->op == OP_LOAD_NULL) {
        Expr *e = expr_new(EXPR_NULL);
        if (!e) return IR2AEL_STATUS_OOM;
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }
    if (inst->op == OP_LOAD_STR) {
        Expr *e = expr_new(EXPR_STR);
        if (!e) return IR2AEL_STATUS_OOM;
        e->text = inst->str ? _strdup(inst->str) : _strdup("");
        if (!e->text) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }
    if (inst->op == OP_LOAD_VAR) {
        Expr *e = expr_new(EXPR_VAR);
        if (!e) return IR2AEL_STATUS_OOM;
        e->text = inst->str ? _strdup(inst->str) : _strdup("");
        if (!e->text) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        if (!stack_push(&s->stack, &s->stack_len, &s->stack_cap, e)) {
            expr_free(e);
            return IR2AEL_STATUS_OOM;
        }
        return IR2AEL_STATUS_HANDLED;
    }

    return IR2AEL_STATUS_NOT_HANDLED;
}
