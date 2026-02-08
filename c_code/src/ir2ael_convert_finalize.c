/* ir2ael_convert_finalize.c - end-of-stream cleanup */
#include "ir2ael_internal.h"

Ir2AelStatus ir2ael_finalize(Ir2AelState *s) {
    if (!s) return IR2AEL_STATUS_FAIL;

    if (s->pending_decls.count > 0) {
        int decl_col0 = 0;
        if (s->pending_decls.is_local && (s->in_function || s->pending_defun)) {
            decl_col0 = decl_indent_col0_from_depth(s->pending_decls.depth > 0 ? s->pending_decls.depth : 1);
        }
        if (!decl_group_emit_and_track(s->out, &s->pending_decls, s->out->line0, decl_col0, &s->local_init)) {
            return IR2AEL_STATUS_FAIL_EMIT;
        }
        decl_group_clear(&s->pending_decls);
    }

    if (s->stack_len > 0) {
        int stmt_col0 = s->in_function ? decl_indent_col0_from_depth(s->cur_depth > 0 ? s->cur_depth : 1) : 0;
        for (size_t si = 0; si < s->stack_len; si++) {
            Expr *e = s->stack[si];
            if (!e) continue;
            if (!ael_emit_at(s->out, s->out->line0, stmt_col0)) return IR2AEL_STATUS_FAIL_EMIT;
            if (!emit_expr_addr(s->out, e, 0)) return IR2AEL_STATUS_FAIL_EMIT;
            if (!ael_emit_text(s->out, ";\n")) return IR2AEL_STATUS_FAIL_EMIT;
        }
        stack_clear(s->stack, &s->stack_len);
    }

    if (s->pending_defun) {
        if (!ael_emit_at(s->out, s->pending_defun_line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, "defun ")) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, s->pending_defun_name[0] ? s->pending_defun_name : "f")) {
            return IR2AEL_STATUS_FAIL_EMIT;
        }
        if (!ael_emit_text(s->out, "()\n{\n")) return IR2AEL_STATUS_FAIL_EMIT;
        s->pending_defun = false;
        s->in_function = true;
        s->function_brace_open = true;
        s->current_defun_line0 = s->pending_defun_line0;
        local_init_clear(&s->local_init);
    }

    bool needs_function_close = s->in_function || s->function_brace_open;
    if (!needs_function_close) {
        while (s->if_sp > 0) {
            IfCtx *ctx = &s->if_stack[s->if_sp - 1];
            if (ctx->brace_style || (ctx->stage == 2 && ctx->else_brace_style)) {
                if (s->out->col0 != 0) {
                    if (!ael_emit_char(s->out, '\n')) return IR2AEL_STATUS_FAIL_EMIT;
                }
                if (!ael_emit_at(s->out, s->out->line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
                if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
                anon_depth_pop_expected(&s->anon_depth_sp, s->anon_depth_stack, ctx->depth + 1, true);
            }
            s->if_sp--;
        }
        s->if_sp = 0;
    }

    if (needs_function_close) {
        while (s->if_sp > 0) {
            IfCtx *ctx = &s->if_stack[s->if_sp - 1];
            if (ctx->brace_style || (ctx->stage == 2 && ctx->else_brace_style)) {
                int close_col0 = ctx->depth * 4;
                if (s->out->col0 != 0) {
                    if (!ael_emit_char(s->out, '\n')) return IR2AEL_STATUS_FAIL_EMIT;
                }
                if (!ael_emit_at(s->out, s->out->line0, close_col0)) return IR2AEL_STATUS_FAIL_EMIT;
                if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
                anon_depth_pop_expected(&s->anon_depth_sp, s->anon_depth_stack, ctx->depth + 1, true);
            }
            s->if_sp--;
        }
        if (!anon_close_scope_blocks_at_function_end(s->out, &s->anon_depth_sp, s->anon_depth_stack, 1, s->out->line0)) {
            return IR2AEL_STATUS_FAIL_EMIT;
        }
        if (!ael_emit_at(s->out, s->out->line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
        s->in_function = false;
        s->function_brace_open = false;
        local_init_clear(&s->local_init);
        s->anon_depth_sp = 0;
        s->if_sp = 0;
        s->loop_sp = 0;
        s->for_hdr_sp = 0;
        s->sw.active = false;
    }

    if (s->global_local_block_open) {
        if (!ael_emit_at(s->out, s->out->line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
        s->global_local_block_open = false;
    }

    return IR2AEL_STATUS_NOT_HANDLED;
}
