/* ir2ael_convert_scope.c - scope bookkeeping handlers */
#include "ir2ael_internal.h"
#include <string.h>

Ir2AelStatus ir2ael_handle_scope_ops(Ir2AelState *s, size_t *i, const IRInst *inst) {
    if (!s || !i || !inst) return IR2AEL_STATUS_FAIL;

    if (inst->op == OP_NUM_LOCAL) {
        if (s->in_function && s->stack_len == 1 && s->stack[0] && s->stack[0]->kind == EXPR_VAR &&
            *i + 4 < s->program->count &&
            s->program->insts[*i + 1].op == OP_NUM_LOCAL &&
            s->program->insts[*i + 2].op == OP_LOAD_NULL &&
            s->program->insts[*i + 3].op == OP_OP && s->program->insts[*i + 3].has_arg1 && s->program->insts[*i + 3].arg1 == 20 &&
            s->program->insts[*i + 4].op == OP_DEFINE_FUNCT) {

            const IRInst *ret_inst = &s->program->insts[*i + 3];
            int stmt_line0 = ret_inst->has_arg2 ? ret_inst->arg2 : s->out->line0;
            int rcol0 = ret_inst->has_arg3 ? ret_inst->arg3 : 0;

            Expr *lhs = stack_pop(s->stack, &s->stack_len);
            if (lhs && lhs->kind == EXPR_VAR && lhs->text) {
                int name_len = (int)strlen(lhs->text);
                int indent_col0 = rcol0 - name_len - 4;
                if (indent_col0 < 0) indent_col0 = 0;

                if (!switch_emit_pending_case_before_stmt(s->out, &s->sw, stmt_line0, indent_col0, &s->anon_depth_sp, s->anon_depth_stack)) {
                    expr_free(lhs);
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
                if (s->in_function && !anon_close_scopes_before_stmt(s->out, &s->anon_depth_sp, s->anon_depth_stack, s->cur_depth, stmt_line0)) {
                    expr_free(lhs);
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
                if (!ael_emit_at(s->out, stmt_line0, indent_col0)) {
                    expr_free(lhs);
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
                if (!ael_emit_text(s->out, lhs->text)) {
                    expr_free(lhs);
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
                expr_free(lhs);
                if (!ael_emit_text(s->out, " = {};")) return IR2AEL_STATUS_FAIL_EMIT;
                if (!ael_emit_text(s->out, "\n")) return IR2AEL_STATUS_FAIL_EMIT;
            } else {
                expr_free(lhs);
            }

            stack_clear(s->stack, &s->stack_len);
            decl_group_clear(&s->pending_decls);
            *i += 3;
            return IR2AEL_STATUS_HANDLED;
        }

        if (s->in_function && s->loop_sp > 0 && s->loop_stack[s->loop_sp - 1].kind == 2 &&
            !s->loop_stack[s->loop_sp - 1].header_emitted && inst->has_depth) {
            LoopCtx *lctx = &s->loop_stack[s->loop_sp - 1];
            if (lctx->depth > 0 && inst->depth == lctx->depth + 1) {
                int stmt_line0 = -1;
                int stmt_col0 = 0;
                for (size_t j = *i + 1; j < s->program->count && j < *i + 512; j++) {
                    const IRInst *mj = &s->program->insts[j];
                    if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
                    if (mj->op == OP_END_LOOP) break;
                    if (mj->op == OP_OP && mj->has_arg2 && mj->has_arg3) {
                        stmt_line0 = mj->arg2;
                        stmt_col0 = mj->arg3;
                        break;
                    }
                }

                if (!s->out->strict_pos || stmt_line0 < 0) {
                    if (!ael_emit_text(s->out, "do {\n")) return IR2AEL_STATUS_FAIL_EMIT;
                } else if (lctx->while_col0 == 0) {
                    int do_line0 = stmt_line0 - 2;
                    if (do_line0 < 0) do_line0 = 0;
                    if (!ael_emit_at(s->out, do_line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
                    if (!ael_emit_text(s->out, "do\n")) return IR2AEL_STATUS_FAIL_EMIT;
                    if (!ael_emit_at(s->out, do_line0 + 1, 0)) return IR2AEL_STATUS_FAIL_EMIT;
                    if (!ael_emit_text(s->out, "{\n")) return IR2AEL_STATUS_FAIL_EMIT;
                } else {
                    int do_line0 = stmt_line0 - 1;
                    int do_col0 = stmt_col0 - 4;
                    if (do_line0 < 0) do_line0 = 0;
                    if (do_col0 < 0) do_col0 = 0;
                    if (!ael_emit_at(s->out, do_line0, do_col0)) return IR2AEL_STATUS_FAIL_EMIT;
                    if (!ael_emit_text(s->out, "do {\n")) return IR2AEL_STATUS_FAIL_EMIT;
                }

                lctx->header_emitted = true;
                (void)anon_depth_push_marked(&s->anon_depth_sp, s->anon_depth_stack, lctx->depth + 1, true);
            }
        }

        if (s->out->allow_num_local_scope_blocks && s->in_function && inst->has_depth && inst->depth > 1) {
            int target_depth = inst->depth;
            int cur_abs_depth = anon_block_current_depth(s->anon_depth_stack, s->anon_depth_sp);
            if (cur_abs_depth < 1) cur_abs_depth = 1;
            bool allow_open = (!s->out->strict_pos) ? true : num_local_should_open_scope_block(s->program, *i, target_depth);
            if (target_depth > cur_abs_depth && s->stack_len == 0 && allow_open) {
                if (s->pending_decls.count > 0 && s->pending_decls.in_function && s->pending_decls.depth > 0 &&
                    s->pending_decls.depth < target_depth) {
                    int decl_col0 = decl_indent_col0_from_depth(s->pending_decls.depth);
                    if (!decl_group_emit_and_track(s->out, &s->pending_decls, s->out->line0, decl_col0, &s->local_init)) {
                        return IR2AEL_STATUS_FAIL_EMIT;
                    }
                    decl_group_clear(&s->pending_decls);
                }
                int brace_line0 = s->out->line0;
                if (!anon_block_open_to(s->out, &s->anon_depth_sp, s->anon_depth_stack, target_depth, brace_line0)) {
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
            }
        }
        return IR2AEL_STATUS_HANDLED;
    }

    if (inst->op == OP_DROP_LOCAL) {
        if (s->global_local_block_open) {
            if (s->pending_decls.count > 0) {
                int decl_col0 = s->pending_decls.is_local ?
                    decl_indent_col0_from_depth(s->pending_decls.depth > 0 ? s->pending_decls.depth : 1) : 0;
                if (!decl_group_emit_and_track(s->out, &s->pending_decls, s->out->line0, decl_col0, &s->local_init)) {
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
                decl_group_clear(&s->pending_decls);
            }
            if (!ael_emit_at(s->out, s->out->line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
            if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
            s->global_local_block_open = false;
        }
        if (s->out->allow_num_local_scope_blocks && s->in_function && s->anon_depth_sp > 0) {
            int v = s->anon_depth_stack[s->anon_depth_sp - 1];
            if (v > 0) {
                int col0 = decl_indent_col0_from_depth(v - 1);
                if (!ael_emit_at(s->out, s->out->line0, col0)) return IR2AEL_STATUS_FAIL_EMIT;
                if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
                s->anon_depth_sp--;
            }
        }
        return IR2AEL_STATUS_HANDLED;
    }

    return IR2AEL_STATUS_NOT_HANDLED;
}
