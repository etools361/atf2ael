/* ir2ael_convert_expr.c - expression handling */
#include "ir2ael_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Ir2AelStatus ir2ael_handle_expr_ops(Ir2AelState *s, size_t *idx, const IRInst *inst) {
    if (!s || !inst || !idx) return IR2AEL_STATUS_FAIL;
    Ir2AelState *st = s;
    const IRProgram *program = s->program;
    AelEmitter *out = s->out;
    char *err = s->err;
    size_t err_cap = s->err_cap;
    size_t i = *idx;
    Ir2AelStatus sub_rc;

#define RETURN_STATUS(_status) do { *idx = i; return (_status); } while (0)

    if (inst->op == OP_OP) {
        do {
            int op_code = inst->has_arg1 ? inst->arg1 : 0;

            /* Ternary template: cond ? then : else
             * Note: OP_ADD_LABEL arg1 is not reliable; use BRANCH_TRUE / SET_LABEL arg1 as label ids.
             *
             * Some IR emitters insert locals bookkeeping (NUM_LOCAL/DROP_LOCAL) between template markers;
             * skip them while matching so ATF-derived IR doesn't break the pattern.
             */
            if (op_code == 59) {
                size_t p1 = ir_skip_scope_bookkeeping(program, i + 1);
                size_t p2 = ir_skip_scope_bookkeeping(program, p1 + 1);
                size_t p3 = ir_skip_scope_bookkeeping(program, p2 + 1);
                size_t p4 = ir_skip_scope_bookkeeping(program, p3 + 1);
                size_t p5 = ir_skip_scope_bookkeeping(program, p4 + 1);
                if (p5 < program->count &&
                    program->insts[p1].op == OP_ADD_LABEL &&
                    program->insts[p2].op == OP_ADD_LABEL &&
                    program->insts[p3].op == OP_OP && program->insts[p3].has_arg1 && program->insts[p3].arg1 == 3 &&
                    program->insts[p4].op == OP_BRANCH_TRUE && program->insts[p4].has_arg1 &&
                    program->insts[p5].op == OP_OP && program->insts[p5].has_arg1 && program->insts[p5].arg1 == 61) {

                    int false_label = program->insts[p4].arg1;
                    size_t then_start = p5 + 1;

                    size_t idx_op60 = (size_t)-1;
                    size_t idx_load_true = (size_t)-1;
                    size_t idx_branch_end = (size_t)-1;
                    size_t idx_set_false = (size_t)-1;
                    for (size_t j = then_start; j < program->count; j++) {
                        if (!(program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == 60)) continue;
                        size_t k1 = ir_skip_scope_bookkeeping(program, j + 1);
                        size_t k2 = ir_skip_scope_bookkeeping(program, k1 + 1);
                        size_t k3 = ir_skip_scope_bookkeeping(program, k2 + 1);
                        if (k3 >= program->count) break;
                        if (ir_inst_is_load_trueish(&program->insts[k1]) &&
                            program->insts[k2].op == OP_BRANCH_TRUE && program->insts[k2].has_arg1 &&
                            program->insts[k3].op == OP_SET_LABEL && program->insts[k3].has_arg1 &&
                            program->insts[k3].arg1 == false_label) {
                            idx_op60 = j;
                            idx_load_true = k1;
                            idx_branch_end = k2;
                            idx_set_false = k3;
                            break;
                        }
                    }

                    if (idx_op60 != (size_t)-1) {
                        int end_label = program->insts[idx_branch_end].arg1;
                        size_t else_start = idx_set_false + 1;
                        size_t idx_op65 = (size_t)-1;
                        size_t idx_set_end = (size_t)-1;
                        for (size_t j = else_start; j < program->count; j++) {
                            if (!(program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == 65)) continue;
                            size_t k1 = ir_skip_scope_bookkeeping(program, j + 1);
                            if (k1 >= program->count) break;
                            if (program->insts[k1].op == OP_SET_LABEL && program->insts[k1].has_arg1 &&
                                program->insts[k1].arg1 == end_label) {
                                idx_op65 = j;
                                idx_set_end = k1;
                                break;
                            }
                        }

                        if (idx_op65 != (size_t)-1) {
                            Expr *cond = stack_pop(st->stack, &st->stack_len);
                            if (!cond) {
                                if (err && err_cap) snprintf(err, err_cap, "ternary without condition at IR index %zu", i);
                                goto fail;
                            }
                            Expr *then_e = NULL;
                            Expr *else_e = NULL;
                            if (!parse_expr_range(program, then_start, idx_op60, &then_e, err, err_cap)) {
                                expr_free(cond);
                                goto fail;
                            }
                            if (!parse_expr_range(program, else_start, idx_op65, &else_e, err, err_cap)) {
                                expr_free(cond);
                                expr_free(then_e);
                                goto fail;
                            }

                            Expr *te = expr_new(EXPR_TERNARY);
                            if (!te) {
                                expr_free(cond);
                                expr_free(then_e);
                                expr_free(else_e);
                                goto oom;
                            }
                            te->lhs = cond;
                            te->mid = then_e;
                            te->rhs = else_e;
                            te->lparen_line0 = program->insts[p5].has_arg2 ? program->insts[p5].arg2 : -1;
                            te->lparen_col0 = program->insts[p5].has_arg3 ? program->insts[p5].arg3 : -1;
                            te->op_line0 = program->insts[p4].has_arg2 ? program->insts[p4].arg2 : -1;
                            te->op_col0 = program->insts[p4].has_arg3 ? program->insts[p4].arg3 : -1;
                            te->close_line0 = program->insts[idx_branch_end].has_arg2 ? program->insts[idx_branch_end].arg2 : -1;
                            te->close_col0 = program->insts[idx_branch_end].has_arg3 ? program->insts[idx_branch_end].arg3 : -1;
                            if (!stack_push(&st->stack, &st->stack_len, &st->stack_cap, te)) {
                                expr_free(te);
                                goto oom;
                            }

                            i = idx_set_end; /* consume through SET_LABEL end */
                            continue;
                        }
                    }
                }
            }

            /* Short-circuit OR/AND templates (true||false, true&&false style).
             * Note: OP_ADD_LABEL arg1 is not reliable; use BRANCH_TRUE / SET_LABEL arg1 as label ids.
             */
            if ((op_code == 62 || op_code == 63) && i >= 1 &&
                program->insts[i - 1].op == OP_ADD_LABEL) {

                size_t idx_bt = (size_t)-1;
                int end_label = -1;
                if (op_code == 63) {
                    if (i + 2 < program->count &&
                        program->insts[i + 1].op == OP_OP && program->insts[i + 1].has_arg1 && program->insts[i + 1].arg1 == 36 &&
                        program->insts[i + 2].op == OP_BRANCH_TRUE && program->insts[i + 2].has_arg1) {
                        idx_bt = i + 2;
                        end_label = program->insts[i + 2].arg1;
                    }
                } else {
                    if (i + 3 < program->count &&
                        program->insts[i + 1].op == OP_OP && program->insts[i + 1].has_arg1 && program->insts[i + 1].arg1 == 36 &&
                        program->insts[i + 2].op == OP_OP && program->insts[i + 2].has_arg1 && program->insts[i + 2].arg1 == 3 &&
                        program->insts[i + 3].op == OP_BRANCH_TRUE && program->insts[i + 3].has_arg1) {
                        idx_bt = i + 3;
                        end_label = program->insts[i + 3].arg1;
                    }
                }

                if (idx_bt != (size_t)-1 && idx_bt + 2 < program->count &&
                    program->insts[idx_bt + 1].op == OP_OP && program->insts[idx_bt + 1].has_arg1 && program->insts[idx_bt + 1].arg1 == 0 &&
                    program->insts[idx_bt + 2].op == OP_OP && program->insts[idx_bt + 2].has_arg1 && program->insts[idx_bt + 2].arg1 == op_code) {

                    size_t rhs_start = idx_bt + 3;
                    size_t rhs_marker = (size_t)-1;
                    for (size_t j = rhs_start; j + 1 < program->count; j++) {
                        if (program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == op_code &&
                            program->insts[j + 1].op == OP_SET_LABEL && program->insts[j + 1].has_arg1 && program->insts[j + 1].arg1 == end_label) {
                            rhs_marker = j;
                            break;
                        }
                    }

                    if (rhs_marker != (size_t)-1) {
                        Expr *lhs = stack_pop(st->stack, &st->stack_len);
                        if (!lhs) {
                            if (err && err_cap) snprintf(err, err_cap, "short-circuit without lhs at IR index %zu", i);
                            goto fail;
                        }
                        Expr *rhs = NULL;
                        if (!parse_expr_range(program, rhs_start, rhs_marker, &rhs, err, err_cap)) {
                            /* Fallback: include the trailing marker + SET_LABEL so nested short-circuit templates
                               inside RHS can be recognized and consumed by parse_expr_range. */
                            size_t rhs_end = rhs_marker + 2;
                            if (rhs_end > program->count) rhs_end = program->count;
                            if (!parse_expr_range(program, rhs_start, rhs_end, &rhs, err, err_cap)) {
                            expr_free(lhs);
                            goto fail;
                            }
                        }
                        Expr *e = expr_new(EXPR_BINOP);
                        if (!e) {
                            expr_free(lhs);
                            expr_free(rhs);
                            goto oom;
                        }
                        e->op_code = (op_code == 63) ? 19 : 18;
                        e->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
                        e->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
                        e->lhs = lhs;
                        e->rhs = rhs;
                        if (!stack_push(&st->stack, &st->stack_len, &st->stack_cap, e)) {
                            expr_free(e);
                            goto oom;
                        }

                        i = rhs_marker + 1; /* consume through SET_LABEL end */
                        continue;
                    }
                }
            }

            /* Ternary expression template (top-level):
             *   <cond expr ...>
             *   OP=59
             *   ADD_LABEL (false)
             *   ADD_LABEL (end)
             *   OP=3
             *   BRANCH_TRUE false
             *   OP=61
             *   <then expr ...>
             *   OP=60
             *   LOAD_TRUE
             *   BRANCH_TRUE end
             *   SET_LABEL false
             *   <else expr ...>
             *   OP=65
             *   SET_LABEL end
             */
            if (op_code == 59 && i + 5 < program->count &&
                program->insts[i + 1].op == OP_ADD_LABEL &&
                program->insts[i + 2].op == OP_ADD_LABEL &&
                program->insts[i + 3].op == OP_OP && program->insts[i + 3].has_arg1 && program->insts[i + 3].arg1 == 3 &&
                program->insts[i + 4].op == OP_BRANCH_TRUE && program->insts[i + 4].has_arg1 &&
                program->insts[i + 5].op == OP_OP && program->insts[i + 5].has_arg1 && program->insts[i + 5].arg1 == 61) {

                int false_label = program->insts[i + 4].arg1;

                size_t idx_op60 = (size_t)-1;
                for (size_t j = i + 6; j + 3 < program->count; j++) {
                    if (program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == 60 &&
                        ir_inst_is_load_trueish(&program->insts[j + 1]) &&
                        program->insts[j + 2].op == OP_BRANCH_TRUE && program->insts[j + 2].has_arg1 &&
                        program->insts[j + 3].op == OP_SET_LABEL && program->insts[j + 3].has_arg1 &&
                        program->insts[j + 3].arg1 == false_label) {
                        idx_op60 = j;
                        break;
                    }
                }

                if (idx_op60 != (size_t)-1) {
                    int end_label = program->insts[idx_op60 + 2].arg1;
                    size_t else_start = idx_op60 + 4;
                    size_t idx_op65 = (size_t)-1;
                    for (size_t j = else_start; j + 1 < program->count; j++) {
                        if (program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == 65 &&
                            program->insts[j + 1].op == OP_SET_LABEL && program->insts[j + 1].has_arg1 &&
                            program->insts[j + 1].arg1 == end_label) {
                            idx_op65 = j;
                            break;
                        }
                    }

                    if (idx_op65 != (size_t)-1) {
                        Expr *cond = stack_pop(st->stack, &st->stack_len);
                        stack_clear(st->stack, &st->stack_len);
                        if (!cond) {
                            if (err && err_cap) snprintf(err, err_cap, "ternary without condition at IR index %zu", i);
                            goto fail;
                        }
                        Expr *then_e = NULL;
                        Expr *else_e = NULL;
                        if (!parse_expr_range(program, i + 6, idx_op60, &then_e, err, err_cap)) {
                            expr_free(cond);
                            goto fail;
                        }
                        if (!parse_expr_range(program, else_start, idx_op65, &else_e, err, err_cap)) {
                            expr_free(cond);
                            expr_free(then_e);
                            goto fail;
                        }
                        Expr *te = expr_new(EXPR_TERNARY);
                        if (!te) {
                            expr_free(cond);
                            expr_free(then_e);
                            expr_free(else_e);
                            goto oom;
                        }
                        te->lhs = cond;
                        te->mid = then_e;
                        te->rhs = else_e;
                        te->lparen_line0 = program->insts[i + 5].has_arg2 ? program->insts[i + 5].arg2 : -1;
                        te->lparen_col0 = program->insts[i + 5].has_arg3 ? program->insts[i + 5].arg3 : -1;
                        te->op_line0 = program->insts[i + 4].has_arg2 ? program->insts[i + 4].arg2 : -1;
                        te->op_col0 = program->insts[i + 4].has_arg3 ? program->insts[i + 4].arg3 : -1;
                        te->close_line0 = program->insts[idx_op60 + 2].has_arg2 ? program->insts[idx_op60 + 2].arg2 : -1;
                        te->close_col0 = program->insts[idx_op60 + 2].has_arg3 ? program->insts[idx_op60 + 2].arg3 : -1;
                        if (!stack_push(&st->stack, &st->stack_len, &st->stack_cap, te)) {
                            expr_free(te);
                            goto oom;
                        }
                        i = idx_op65 + 1; /* consume through SET_LABEL end */
                        continue;
                    }
                }

                /* Fallback: if the full ternary tail isn't present, treat it as an empty if-statement
                   so conversion can proceed on truncated/minimal IR streams. */
                Expr *cond = stack_pop(st->stack, &st->stack_len);
                stack_clear(st->stack, &st->stack_len);
                if (cond) {
                    int line0 = inst->has_arg2 ? inst->arg2 : out->line0;
                    int if_col0 = if_keyword_col0_from_cond(cond, line0, (st->in_function || st->pending_defun));
                    if (!ael_emit_at(out, line0, if_col0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    int lparen_col0 = if_lparen_col0_from_cond(cond, line0, if_col0);
                    if (!emit_if_keyword_and_lparen(out, line0, if_col0, lparen_col0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    if (!emit_expr_addr(out, cond, 0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    expr_free(cond);
                    if (!ael_emit_text(out, ") {\n}\n")) goto fail_emit;
                    i += 5; /* consume through OP=61 */
                    continue;
                }
            }

            /* if/else pattern start: OP=59 + ADD_LABEL + OP=3 + BRANCH_TRUE */
            if (op_code == 59 && i + 3 < program->count &&
                program->insts[i + 1].op == OP_ADD_LABEL &&
                program->insts[i + 2].op == OP_OP && program->insts[i + 2].has_arg1 && program->insts[i + 2].arg1 == 3 &&
                program->insts[i + 3].op == OP_BRANCH_TRUE && program->insts[i + 3].has_arg1) {

                Expr *cond = stack_pop(st->stack, &st->stack_len);
                stack_clear(st->stack, &st->stack_len);
                if (!cond) {
                    if (err && err_cap) snprintf(err, err_cap, "if without condition at IR index %zu", i);
                    goto fail;
                }

                int line0 = inst->has_arg2 ? inst->arg2 : 0;
                int rparen_col0 = inst->has_arg3 ? inst->arg3 : 0;
                int else_label = program->insts[i + 3].arg1;
                bool immediate_block = false;
                if (i + 4 < program->count && program->insts[i + 4].op == OP_NUM_LOCAL) {
                    immediate_block = true;
                }
                int if_col0_pre = if_keyword_col0_from_cond(cond, line0, (st->in_function || st->pending_defun));
                if (st->pending_decls.count > 0) {
                    int decl_line0 = line0 - 1;
                    if (decl_line0 < out->line0) decl_line0 = out->line0;
                    int decl_col0 = st->pending_decls.in_function ? decl_indent_col0_from_depth(st->pending_decls.depth > 0 ? st->pending_decls.depth : 1) : 0;
                    if (!decl_group_emit_and_track(out, &st->pending_decls, decl_line0, decl_col0, &st->local_init)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    decl_group_clear(&st->pending_decls);
                }
                if (!switch_emit_pending_case_before_stmt(out, &st->sw, line0, if_col0_pre, &st->anon_depth_sp, st->anon_depth_stack)) {
                    expr_free(cond);
                    goto fail_emit;
                }
                if (!if_close_braced_else_on_depth_exit(out, st->if_stack, &st->if_sp, st->cur_depth, &st->anon_depth_sp, st->anon_depth_stack)) goto fail_emit;

                /* Single-statement if for break/continue (no braces): if(cond) continue; / break; */
                if (i + 7 < program->count &&
                    program->insts[i + 4].op == OP_LOAD_TRUE &&
                    (program->insts[i + 5].op == OP_LOOP_AGAIN || program->insts[i + 5].op == OP_LOOP_EXIT) &&
                    program->insts[i + 6].op == OP_BRANCH_TRUE && program->insts[i + 6].has_arg2 && program->insts[i + 6].has_arg3 &&
                    program->insts[i + 7].op == OP_SET_LABEL && program->insts[i + 7].has_arg1 && program->insts[i + 7].arg1 == else_label) {

                    bool is_continue = (program->insts[i + 5].op == OP_LOOP_AGAIN);
                    int stmt_line0 = program->insts[i + 6].arg2;
                    int stmt_col0 = program->insts[i + 6].arg3;
                    int if_col0 = if_keyword_col0_from_cond(cond, line0, (st->in_function || st->pending_defun));

                    if (st->pending_inline_else_if && line0 == st->pending_inline_else_line0) {
                        if (if_col0 < out->col0) if_col0 = out->col0;
                        if (!ael_emit_at(out, line0, if_col0)) {
                            expr_free(cond);
                            goto fail_emit;
                        }
                        st->pending_inline_else_if = false;
                        st->pending_inline_else_line0 = -1;
                        st->pending_inline_else_col0 = -1;
                    } else {
                        if (!ael_emit_at(out, line0, if_col0)) {
                            expr_free(cond);
                            goto fail_emit;
                        }
                    }
                    int lparen_col0 = if_lparen_col0_from_cond(cond, line0, if_col0);
                    if (!emit_if_keyword_and_lparen(out, line0, if_col0, lparen_col0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    if (!emit_expr_addr(out, cond, 0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    expr_free(cond);
                    if (out->col0 > rparen_col0) rparen_col0 = out->col0;
                    if (!ael_emit_at(out, line0, rparen_col0)) goto fail_emit;
                    if (!ael_emit_text(out, ")\n")) goto fail_emit;
                    if (!ael_emit_at(out, stmt_line0, stmt_col0)) goto fail_emit;
                    if (!ael_emit_text(out, is_continue ? "continue;\n" : "break;\n")) goto fail_emit;

                    i += 7;
                    continue;
                }

                /* if(cond) <single stmt>; (no else header, no braces) */
                bool has_else_header = false;
                bool brace_style = true;
                int else_header_br_line0 = -1;
                int inferred_end_label = -1;
                int max_body_depth = st->cur_depth;
                int body_stmt_count = 0;
                bool body_has_num_local = false;

                /* Prefer the last matching else-header template before the else-label, since nested/short-circuit
                   patterns can introduce similar instruction sequences inside the then-body. */
                bool best_has_else_header = false;
                bool best_brace_style = true;
                int best_inferred_end_label = -1;

                for (size_t j = i + 4; j < program->count && j < i + 512; j++) {
                    if (program->insts[j].op == OP_SET_LABEL && program->insts[j].has_arg1 &&
                        program->insts[j].arg1 == else_label) {
                        break;
                    }
                    bool depth_ok = (!program->insts[j].has_depth || program->insts[j].depth == st->cur_depth ||
                                     !out->allow_num_local_scope_blocks);
                    if (program->insts[j].op == OP_NUM_LOCAL) {
                        body_has_num_local = true;
                    }
                    if (program->insts[j].op == OP_OP && program->insts[j].has_arg1 &&
                        (program->insts[j].arg1 == 0 || program->insts[j].arg1 == 20) &&
                        program->insts[j].has_arg2) {
                        body_stmt_count++;
                    }
                    if (program->insts[j].has_depth && program->insts[j].depth > max_body_depth) {
                        max_body_depth = program->insts[j].depth;
                    }
                    /* No-brace else template (compact): ... then-stmt ... LOAD_TRUE; BRANCH_TRUE end; SET_LABEL else */
                    if (j + 2 < program->count &&
                        depth_ok &&
                        program->insts[j].op == OP_LOAD_TRUE &&
                        program->insts[j + 1].op == OP_BRANCH_TRUE && program->insts[j + 1].has_arg1 &&
                        program->insts[j + 2].op == OP_SET_LABEL && program->insts[j + 2].has_arg1 &&
                        program->insts[j + 2].arg1 == else_label) {
                        best_has_else_header = true;
                        /* If an else-block opens immediately (NUM_LOCAL), this is a braced else template. */
                        best_brace_style = (j + 3 < program->count && program->insts[j + 3].op == OP_NUM_LOCAL);
                        best_inferred_end_label = program->insts[j + 1].arg1;
                        continue;
                    }
                    /* No-brace else template: ... then-stmt ... LOAD_TRUE; BRANCH_TRUE end; SET_LABEL else */
                    if (j + 1 < program->count &&
                        depth_ok &&
                        program->insts[j].op == OP_LOAD_TRUE &&
                        program->insts[j + 1].op == OP_BRANCH_TRUE && program->insts[j + 1].has_arg1 &&
                        program->insts[j + 1].arg1 != else_label) {
                        int base_indent = st->cur_depth * 4;
                        int close_col0 = program->insts[j + 1].has_arg3 ? program->insts[j + 1].arg3 : 0;
                        if (close_col0 > base_indent + 8) {
                            /* Guard: only treat this as an else-header if we soon encounter the else-label. */
                            bool has_else_label_soon = false;
                            for (size_t k = j + 2; k < program->count && k < j + 16; k++) {
                                if (program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 &&
                                    program->insts[k].arg1 == else_label) {
                                    has_else_label_soon = true;
                                    break;
                                }
                            }
                            if (has_else_label_soon) {
                                best_has_else_header = true;
                                best_brace_style = false;
                                best_inferred_end_label = program->insts[j + 1].arg1;
                            }
                            continue;
                        }
                    }
                    if (j + 3 < program->count &&
                        depth_ok &&
                        program->insts[j].op == OP_ADD_LABEL &&
                        program->insts[j + 1].op == OP_LOAD_TRUE &&
                        program->insts[j + 2].op == OP_BRANCH_TRUE && program->insts[j + 2].has_arg1 &&
                        program->insts[j + 3].op == OP_SET_LABEL && program->insts[j + 3].has_arg1 &&
                        program->insts[j + 3].arg1 == else_label) {
                        best_has_else_header = true;
                        best_inferred_end_label = program->insts[j + 2].arg1;
                        int close_col0 = program->insts[j + 2].has_arg3 ? program->insts[j + 2].arg3 : 0;
                        int base_indent = (st->in_function || st->pending_defun) ? 4 : 0;
                        /* When the branch position is near the indentation column, it's typically the braced style:
                           if (...) { ... } \n else { ... }.
                           When it's far right, it's the no-brace single-statement template. */
                        best_brace_style = (close_col0 <= base_indent + 8);
                        continue;
                    }
                }
                has_else_header = best_has_else_header;
                brace_style = best_brace_style;
                inferred_end_label = best_inferred_end_label;
                if (has_else_header && max_body_depth <= st->cur_depth) {
                    /* No extra DEPTH in then-body => single-statement if/elseif chain (no braces). */
                    brace_style = false;
                }
                if (has_else_header && immediate_block) {
                    brace_style = true;
                }
                if (has_else_header && !out->allow_num_local_scope_blocks &&
                    (body_has_num_local || body_stmt_count > 1)) {
                    brace_style = true;
                }

                if (!has_else_header) {
                    int if_col0 = if_keyword_col0_from_cond(cond, line0, (st->in_function || st->pending_defun));
                    if (st->pending_inline_else_if && line0 == st->pending_inline_else_line0) {
                        if (if_col0 < out->col0) if_col0 = out->col0;
                        if (!ael_emit_at(out, line0, if_col0)) {
                            expr_free(cond);
                            goto fail_emit;
                        }
                        st->pending_inline_else_if = false;
                        st->pending_inline_else_line0 = -1;
                        st->pending_inline_else_col0 = -1;
                    } else {
                        if (!ael_emit_at(out, line0, if_col0)) {
                            expr_free(cond);
                            goto fail_emit;
                        }
                    }
                    int lparen_col0 = if_lparen_col0_from_cond(cond, line0, if_col0);
                    if (!emit_if_keyword_and_lparen(out, line0, if_col0, lparen_col0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    if (!emit_expr_addr(out, cond, 0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    expr_free(cond);
                    if (out->col0 > rparen_col0) rparen_col0 = out->col0;
                    if (!ael_emit_at(out, line0, rparen_col0)) goto fail_emit;
                    bool need_block = (max_body_depth > st->cur_depth) || body_has_num_local;
                    if (!out->allow_num_local_scope_blocks) {
                        need_block = (body_has_num_local || body_stmt_count > 1);
                    }
                    if (need_block) {
                        bool brace_same_line = (st->in_function || st->pending_defun);
                        if (brace_same_line) {
                            if (!ael_emit_text(out, ") {\n")) goto fail_emit;
                        } else {
                            if (!ael_emit_text(out, ")\n")) goto fail_emit;
                            if (!ael_emit_at(out, line0 + 1, if_col0)) goto fail_emit;
                            if (!ael_emit_text(out, "{\n")) goto fail_emit;
                        }
                        if (st->if_sp < (int)(sizeof(st->if_stack) / sizeof(st->if_stack[0]))) {
                            st->if_stack[st->if_sp].else_label = else_label;
                            st->if_stack[st->if_sp].end_label = -1;
                            st->if_stack[st->if_sp].stage = 1;
                            st->if_stack[st->if_sp].brace_style = true;
                            st->if_stack[st->if_sp].else_brace_style = false;
                            st->if_stack[st->if_sp].depth = st->cur_depth;
                            st->if_sp++;
                        } else {
                            if (err && err_cap) snprintf(err, err_cap, "if st->stack overflow at IR index %zu", i);
                            goto fail;
                        }
                        if (st->anon_depth_sp < (int)(sizeof(st->anon_depth_stack) / sizeof(st->anon_depth_stack[0]))) {
                            (void)anon_depth_push_marked(&st->anon_depth_sp, st->anon_depth_stack, st->cur_depth + 1, true);
                        }
                    } else {
                        /* Some baselines keep the statement on the same line: "if (cond) stmt;". */
                        int inline_stmt_col0 = -1;
                        for (size_t j = i + 4; j < program->count && j < i + 96; j++) {
                            const IRInst *mj = &program->insts[j];
                            if (mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == else_label) break;
                            if (mj->op == OP_OP && mj->has_arg2 && mj->has_arg3 &&
                                mj->arg2 == line0 && mj->arg3 > rparen_col0) {
                                if (inline_stmt_col0 < 0 || mj->arg3 < inline_stmt_col0) inline_stmt_col0 = mj->arg3;
                            }
                        }
                        if (inline_stmt_col0 >= 0) {
                            if (!ael_emit_text(out, ") ")) goto fail_emit;
                        } else {
                            if (!ael_emit_text(out, ")\n")) goto fail_emit;
                        }
                    }

                    i += 3; /* consume header ops */
                    continue;
                }

                int if_col0 = if_keyword_col0_from_cond(cond, line0, (st->in_function || st->pending_defun));
                bool is_inline_else_if = false;
                int inline_else_col0 = -1;
                if (st->pending_inline_else_if && line0 == st->pending_inline_else_line0) {
                    is_inline_else_if = true;
                    inline_else_col0 = st->pending_inline_else_col0;
                    if (if_col0 < out->col0) if_col0 = out->col0;
                    if (!ael_emit_at(out, line0, if_col0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                    st->pending_inline_else_if = false;
                    st->pending_inline_else_line0 = -1;
                    st->pending_inline_else_col0 = -1;
                } else {
                    if (!ael_emit_at(out, line0, if_col0)) {
                        expr_free(cond);
                        goto fail_emit;
                    }
                }
                int lparen_col0 = if_lparen_col0_from_cond(cond, line0, if_col0);
                if (!emit_if_keyword_and_lparen(out, line0, if_col0, lparen_col0)) {
                    expr_free(cond);
                    goto fail_emit;
                }
                if (!emit_expr_addr(out, cond, 0)) {
                    expr_free(cond);
                    goto fail_emit;
                }
                expr_free(cond);
                if (out->col0 > rparen_col0) rparen_col0 = out->col0;
                if (!ael_emit_at(out, line0, rparen_col0)) goto fail_emit;
                if (brace_style) {
                    bool brace_same_line = (st->in_function || st->pending_defun);
                    if (brace_same_line) {
                        if (!ael_emit_text(out, ") {\n")) goto fail_emit;
                    } else {
                        if (!ael_emit_text(out, ")\n")) goto fail_emit;
                        int brace_col0 = if_col0;
                        if (is_inline_else_if && inline_else_col0 >= 0) brace_col0 = inline_else_col0;
                        if (!ael_emit_at(out, line0 + 1, brace_col0)) goto fail_emit;
                        if (!ael_emit_text(out, "{\n")) goto fail_emit;
                    }
                } else {
                    /* Some baselines keep the statement on the same line: "if (cond) stmt;". */
                    int inline_stmt_col0 = -1;
                    for (size_t j = i + 4; j < program->count && j < i + 96; j++) {
                        const IRInst *mj = &program->insts[j];
                        if (mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == else_label) break;
                        if (mj->op == OP_OP && mj->has_arg2 && mj->has_arg3 &&
                            mj->arg2 == line0 && mj->arg3 > rparen_col0) {
                            if (inline_stmt_col0 < 0 || mj->arg3 < inline_stmt_col0) inline_stmt_col0 = mj->arg3;
                        }
                    }
                    if (inline_stmt_col0 >= 0) {
                        if (!ael_emit_text(out, ") ")) goto fail_emit;
                    } else {
                        if (!ael_emit_text(out, ")\n")) goto fail_emit;
                    }
                }

                if (st->if_sp < (int)(sizeof(st->if_stack) / sizeof(st->if_stack[0]))) {
                    st->if_stack[st->if_sp].else_label = else_label;
                    st->if_stack[st->if_sp].end_label = inferred_end_label;
                    st->if_stack[st->if_sp].stage = 1;
                    st->if_stack[st->if_sp].brace_style = brace_style;
                    st->if_stack[st->if_sp].else_brace_style = false;
                    st->if_stack[st->if_sp].depth = st->cur_depth;
                    st->if_sp++;
                } else {
                    if (err && err_cap) snprintf(err, err_cap, "if st->stack overflow at IR index %zu", i);
                    goto fail;
                }
                if (brace_style) {
                    /* Track brace-open depth so scope reconstruction (and later brace closes) stay consistent. */
                    (void)anon_depth_push_marked(&st->anon_depth_sp, st->anon_depth_stack, st->cur_depth + 1, true);
                }

                i += 3; /* consume header ops */
                continue;
            }

            sub_rc = ir2ael_expr_handle_assign_ops(st, &i, inst);
            if (sub_rc == IR2AEL_STATUS_HANDLED) continue;
            if (sub_rc == IR2AEL_STATUS_FAIL_EMIT) goto fail_emit;
            if (sub_rc == IR2AEL_STATUS_OOM) goto oom;
            if (sub_rc == IR2AEL_STATUS_FAIL) goto fail;

            if (op_code == 0) {
                /* STMT_END: sometimes used as ')' position for for-header; otherwise ignore here. */
                if (st->for_hdr_sp > 0 && st->for_hdr_stack[st->for_hdr_sp - 1].stage == 3 &&
                    inst->has_arg2 && inst->has_arg3 &&
                    inst->arg2 == st->for_hdr_stack[st->for_hdr_sp - 1].line0) {

                    int line0 = inst->arg2;
                    int rparen_col0 = inst->arg3;
                    if (!ael_emit_at(out, line0, rparen_col0)) goto fail_emit;

                    /* Determine whether the loop body is a block: parse_for_statement only emits NUM_LOCAL
                       before the body when the body starts with '{'. */
                    bool body_is_block = !out->allow_num_local_scope_blocks;
                    if (!body_is_block) {
                        for (size_t j = i + 1; j < program->count && j < i + 64; j++) {
                            const IRInst *mj = &program->insts[j];
                            if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
                            if (mj->op == OP_NUM_LOCAL && mj->has_depth && mj->depth > st->cur_depth) {
                                body_is_block = true;
                                break;
                            }
                            /* Stop if we reached an anchored statement without seeing a body NUM_LOCAL. */
                            if (mj->op == OP_OP && mj->has_arg2) break;
                        }
                    }

                    /* Prefer baseline brace style for the loop body:
                       - K&R: "for (...) {"
                       - Allman: "for (...)\n{"
                       Infer via the first anchored body statement line when available. */
                    if (!body_is_block) {
                        if (!ael_emit_text(out, ")\n")) goto fail_emit;
                        if (st->loop_sp > 0 && st->loop_stack[st->loop_sp - 1].kind == 3) {
                            st->loop_stack[st->loop_sp - 1].body_has_brace = false;
                        }
                    } else {
                        int body_line0 = -1;
                        for (size_t j = i + 1; j < program->count && j < i + 256; j++) {
                            const IRInst *mj = &program->insts[j];
                            if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
                            if (mj->op == OP_OP && mj->has_arg2) {
                                body_line0 = mj->arg2;
                                break;
                            }
                        }
                        bool brace_same_line = (body_line0 >= 0 && body_line0 <= line0 + 1);

                        if (brace_same_line) {
                            if (!ael_emit_text(out, ") {\n")) goto fail_emit;
                        } else {
                            if (!ael_emit_text(out, ")\n")) goto fail_emit;
                            int for_col0 = 0;
                            if (st->loop_sp > 0 && st->loop_stack[st->loop_sp - 1].kind == 3) {
                                for_col0 = st->loop_stack[st->loop_sp - 1].header_col0 - 4; /* strlen("for ") */
                                if (for_col0 < 0) for_col0 = 0;
                            }
                            if (!ael_emit_at(out, line0 + 1, for_col0)) goto fail_emit;
                            if (!ael_emit_text(out, "{\n")) goto fail_emit;
                        }
                        if (st->loop_sp > 0 && st->loop_stack[st->loop_sp - 1].kind == 3) {
                            st->loop_stack[st->loop_sp - 1].body_has_brace = true;
                        }
                        (void)anon_depth_push_marked(&st->anon_depth_sp, st->anon_depth_stack, st->cur_depth + 1, true);
                    }
                    st->for_hdr_sp--;
                    continue;
                }

                /* do-while footer uses BRANCH_TRUE start_label at 'while' position to close the loop.
                   Some emitters insert a STMT_END at the end of the condition expression; do not
                   consume/clear the condition st->stack in that case. */
                if (st->loop_sp > 0 && st->loop_stack[st->loop_sp - 1].kind == 2 && i + 1 < program->count) {
                    const IRInst *next = &program->insts[i + 1];
                    if (next->op == OP_BRANCH_TRUE && next->has_arg1 &&
                        next->arg1 == st->loop_stack[st->loop_sp - 1].start_label && next->has_arg2) {
                        continue;
                    }
                }

                /* Expression statement (most commonly: function call). */
                if (st->stack_len > 0 && inst->has_arg2 && inst->has_arg3) {
                    int semi_line0 = inst->arg2;
                    int semi_col0 = inst->arg3;
                    Expr *stmt = NULL;
                    if (st->stack_len == 1) {
                        stmt = stack_pop(st->stack, &st->stack_len);
                    } else {
                        stmt = stack_pop_stmt_expr(st->stack, &st->stack_len);
                    }
                    if (stmt) {
                        int stmt_line0 = expr_min_line0(stmt);
                        if (stmt_line0 < 0) stmt_line0 = semi_line0;
                        int start_col0 = expr_start_col0(stmt, stmt_line0);
                        if (!switch_emit_pending_case_before_stmt(out, &st->sw, stmt_line0, start_col0, &st->anon_depth_sp, st->anon_depth_stack)) {
                            expr_free(stmt);
                            goto fail_emit;
                        }
                        if (!if_close_braced_else_on_depth_exit(out, st->if_stack, &st->if_sp, st->cur_depth, &st->anon_depth_sp, st->anon_depth_stack)) {
                            expr_free(stmt);
                            goto fail_emit;
                        }
                        if (st->in_function && !anon_close_scopes_before_stmt(out, &st->anon_depth_sp, st->anon_depth_stack, st->cur_depth, stmt_line0)) {
                            expr_free(stmt);
                            goto fail_emit;
                        }
                        if (!ael_emit_at(out, stmt_line0, start_col0)) {
                            expr_free(stmt);
                            goto fail_emit;
                        }
                        if (!emit_expr_addr(out, stmt, 0)) {
                            expr_free(stmt);
                            goto fail_emit;
                        }
                        expr_free(stmt);
                        if (semi_line0 > out->line0 || (semi_line0 == out->line0 && semi_col0 >= out->col0)) {
                            if (!ael_emit_at(out, semi_line0, semi_col0)) goto fail_emit;
                        }
                        if (!ael_emit_text(out, ";\n")) goto fail_emit;
                        stack_clear(st->stack, &st->stack_len);
                        decl_group_clear(&st->pending_decls);
                        continue;
                    }
                    stack_clear(st->stack, &st->stack_len);
                    decl_group_clear(&st->pending_decls);
                    continue;
                }

                /* else: assignments handle this; ignore */
                continue;
            }

            if (op_code == 20) {
                /* RETURN: if followed by DEFINE_FUNCT it's the implicit function epilogue, else explicit. */
                if (i + 1 < program->count && program->insts[i + 1].op == OP_DEFINE_FUNCT) {
                    continue;
                }
                /* Pop optional return value (if any), otherwise emit bare return. */
                Expr *ret = stack_pop(st->stack, &st->stack_len);
                int rline0 = inst->has_arg2 ? inst->arg2 : 0;
                int rcol0 = inst->has_arg3 ? inst->arg3 : 0;
                if (!switch_emit_pending_case_before_stmt(out, &st->sw, rline0, rcol0, &st->anon_depth_sp, st->anon_depth_stack)) {
                    expr_free(ret);
                    goto fail_emit;
                }
                if (!if_close_braced_else_on_depth_exit(out, st->if_stack, &st->if_sp, st->cur_depth, &st->anon_depth_sp, st->anon_depth_stack)) {
                    expr_free(ret);
                    goto fail_emit;
                }
                if (st->in_function && !anon_close_scopes_before_stmt(out, &st->anon_depth_sp, st->anon_depth_stack, st->cur_depth, rline0)) {
                    expr_free(ret);
                    goto fail_emit;
                }
                if (!ael_emit_at(out, rline0, rcol0)) {
                    expr_free(ret);
                    goto fail_emit;
                }
                if (!ael_emit_text(out, "return")) {
                    expr_free(ret);
                    goto fail_emit;
                }
                if (ret) {
                    if (!ael_emit_char(out, ' ')) {
                        expr_free(ret);
                        goto fail_emit;
                    }
                    if (!emit_expr_addr(out, ret, 0)) {
                        expr_free(ret);
                        goto fail_emit;
                    }
                    expr_free(ret);
                }
                if (!ael_emit_text(out, ";\n")) goto fail_emit;
                stack_clear(st->stack, &st->stack_len);
                continue;
            }

            sub_rc = ir2ael_expr_handle_list_call_ops(st, &i, inst);
            if (sub_rc == IR2AEL_STATUS_HANDLED) continue;
            if (sub_rc == IR2AEL_STATUS_FAIL_EMIT) goto fail_emit;
            if (sub_rc == IR2AEL_STATUS_OOM) goto oom;
            if (sub_rc == IR2AEL_STATUS_FAIL) goto fail;

            sub_rc = ir2ael_expr_handle_misc_ops(st, &i, inst);
            if (sub_rc == IR2AEL_STATUS_HANDLED) continue;
            if (sub_rc == IR2AEL_STATUS_FAIL_EMIT) goto fail_emit;
            if (sub_rc == IR2AEL_STATUS_OOM) goto oom;
            if (sub_rc == IR2AEL_STATUS_FAIL) goto fail;

        } while (0);
        RETURN_STATUS(IR2AEL_STATUS_HANDLED);
    }

    RETURN_STATUS(IR2AEL_STATUS_NOT_HANDLED);
fail_emit:
    RETURN_STATUS(IR2AEL_STATUS_FAIL_EMIT);
oom:
    RETURN_STATUS(IR2AEL_STATUS_OOM);
fail:
    RETURN_STATUS(IR2AEL_STATUS_FAIL);
#undef RETURN_STATUS
}
