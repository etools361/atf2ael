/* ir2ael_convert_decl.c - defun and declaration handling */
#include "ir2ael_internal.h"
#include <string.h>

Ir2AelStatus ir2ael_preprocess_inst(Ir2AelState *s, size_t i, const IRInst *inst) {
    if (!s || !inst) return IR2AEL_STATUS_FAIL;

    if (inst->has_depth) s->cur_depth = inst->depth;

    if (s->pending_defun && inst->op != 45) {
        if (!ael_emit_at(s->out, s->pending_defun_line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, "defun ")) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, s->pending_defun_name)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_char(s->out, '(')) return IR2AEL_STATUS_FAIL_EMIT;
        for (int pi = 0; pi < s->pending_defun_param_count; pi++) {
            if (pi != 0) {
                if (!ael_emit_text(s->out, ",")) return IR2AEL_STATUS_FAIL_EMIT;
            }
            if (!ael_emit_text(s->out, s->pending_defun_params[pi])) return IR2AEL_STATUS_FAIL_EMIT;
        }
        if (!ael_emit_text(s->out, ")\n")) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_at(s->out, s->pending_defun_line0 + 1, 0)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, "{\n")) return IR2AEL_STATUS_FAIL_EMIT;
        s->pending_defun = false;
        s->in_function = true;
        s->function_brace_open = true;
        s->current_defun_line0 = s->pending_defun_line0;
        s->anon_depth_sp = 0;
        local_init_clear(&s->local_init);
    }

    if (s->pending_decls.count > 1 && inst->op != OP_ADD_GLOBAL && inst->op != OP_ADD_LOCAL &&
        (!s->pending_decls.is_local || s->pending_decls.depth <= 1 || !s->out->strict_pos)) {
        int decl_line0 = s->out->line0;
        int decl_col0 = s->pending_decls.is_local ?
            decl_indent_col0_from_depth(s->pending_decls.depth > 0 ? s->pending_decls.depth : 1) : 0;

        if (s->pending_decls.is_local && inst->op == OP_LOAD_VAR && inst->str &&
            strcmp(inst->str, s->pending_decls.names[s->pending_decls.count - 1]) == 0) {

            DeclGroup prefix = s->pending_decls;
            prefix.count = s->pending_decls.count - 1;
            if (prefix.count > 0) {
                if (!decl_group_emit_and_track(s->out, &prefix, decl_line0, decl_col0, &s->local_init)) {
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
            }
            strncpy(s->pending_decls.names[0], s->pending_decls.names[s->pending_decls.count - 1], 255);
            s->pending_decls.names[0][255] = '\0';
            s->pending_decls.count = 1;
        } else {
            if (!decl_group_emit_and_track(s->out, &s->pending_decls, decl_line0, decl_col0, &s->local_init)) {
                return IR2AEL_STATUS_FAIL_EMIT;
            }
            decl_group_clear(&s->pending_decls);
        }
    }

    if (s->pending_decls.count == 1 && s->stack_len == 0 && inst->op != OP_ADD_GLOBAL && inst->op != OP_ADD_LOCAL) {
        bool is_pending_name_load = (inst->op == OP_LOAD_VAR && inst->str &&
                                     strcmp(inst->str, s->pending_decls.names[0]) == 0);
        if (!is_pending_name_load) {
            bool has_init_soon = scan_for_assignment_to_var(s->program, i, 128, s->pending_decls.names[0],
                                                           s->pending_decls.depth, s->out->strict_pos);
            if (!has_init_soon) {
                int decl_line0 = s->out->line0;
                int decl_col0 = s->pending_decls.in_function ?
                    decl_indent_col0_from_depth(s->pending_decls.depth > 0 ? s->pending_decls.depth : 1) : 0;
                if (!decl_group_emit_and_track(s->out, &s->pending_decls, decl_line0, decl_col0, &s->local_init)) {
                    return IR2AEL_STATUS_FAIL_EMIT;
                }
                decl_group_clear(&s->pending_decls);
            }
        }
    }

    return IR2AEL_STATUS_NOT_HANDLED;
}

Ir2AelStatus ir2ael_handle_function_ops(Ir2AelState *s, size_t i, const IRInst *inst) {
    if (!s || !inst) return IR2AEL_STATUS_FAIL;

    if (inst->op == OP_BEGIN_FUNCT) {
        if (s->pending_defun) {
            if (!ael_emit_at(s->out, s->pending_defun_line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
            if (!ael_emit_text(s->out, "defun ")) return IR2AEL_STATUS_FAIL_EMIT;
            if (!ael_emit_text(s->out, s->pending_defun_name[0] ? s->pending_defun_name : "f")) {
                return IR2AEL_STATUS_FAIL_EMIT;
            }
            if (!ael_emit_char(s->out, '(')) return IR2AEL_STATUS_FAIL_EMIT;
            for (int pi = 0; pi < s->pending_defun_param_count; pi++) {
                if (pi != 0) {
                    if (!ael_emit_text(s->out, ",")) return IR2AEL_STATUS_FAIL_EMIT;
                }
                if (!ael_emit_text(s->out, s->pending_defun_params[pi])) return IR2AEL_STATUS_FAIL_EMIT;
            }
            if (!ael_emit_text(s->out, ")\n{\n}\n")) return IR2AEL_STATUS_FAIL_EMIT;
            s->pending_defun = false;
            s->pending_defun_param_count = 0;
        }
        if (s->in_function) {
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

        if (s->pending_decls.count > 0) {
            if (!decl_group_emit_and_track(s->out, &s->pending_decls, s->out->line0, 0, &s->local_init)) {
                return IR2AEL_STATUS_FAIL_EMIT;
            }
            decl_group_clear(&s->pending_decls);
        }

        s->pending_defun = true;
        s->pending_defun_param_count = 0;
        s->pending_defun_line0 = inst->has_arg1 ? inst->arg1 : 0;
        strncpy(s->pending_defun_name, inst->str ? inst->str : "f", sizeof(s->pending_defun_name) - 1);
        s->pending_defun_name[sizeof(s->pending_defun_name) - 1] = '\0';
        return IR2AEL_STATUS_HANDLED;
    }

    if (inst->op == 45) {
        if (s->pending_defun && s->pending_defun_param_count < 32 && inst->str) {
            strncpy(s->pending_defun_params[s->pending_defun_param_count], inst->str, 255);
            s->pending_defun_params[s->pending_defun_param_count][255] = '\0';
            s->pending_defun_param_count++;
        }
        return IR2AEL_STATUS_HANDLED;
    }

    if (inst->op == OP_DEFINE_FUNCT) {
        int end_line0 = inst->has_arg2 ? inst->arg2 : s->out->line0;
        int end_col0 = inst->has_arg3 ? inst->arg3 : 0;

        int next_begin_line0 = -1;
        for (size_t j = i + 1; j < s->program->count && j < i + 512; j++) {
            const IRInst *n = &s->program->insts[j];
            if (n->op == OP_BEGIN_FUNCT && n->has_arg1) {
                next_begin_line0 = n->arg1;
                break;
            }
        }
        if (next_begin_line0 >= 0 && end_line0 >= next_begin_line0) {
            end_line0 = next_begin_line0 - 1;
        }
        if (end_line0 < s->out->line0) end_line0 = s->out->line0;

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
        s->pending_inline_else_if = false;
        s->pending_inline_else_line0 = -1;
        s->pending_inline_else_col0 = -1;

        if (s->in_function) {
            if (!anon_close_scope_blocks_at_function_end(s->out, &s->anon_depth_sp, s->anon_depth_stack, 1, end_line0)) {
                return IR2AEL_STATUS_FAIL_EMIT;
            }
            s->anon_depth_sp = 0;
        }
        if (end_col0 < 0) end_col0 = 0;
        if (!ael_emit_at(s->out, end_line0, end_col0)) return IR2AEL_STATUS_FAIL_EMIT;
        if (!ael_emit_text(s->out, "}\n")) return IR2AEL_STATUS_FAIL_EMIT;
        s->in_function = false;
        s->function_brace_open = false;
        local_init_clear(&s->local_init);
        decl_group_clear(&s->pending_decls);
        stack_clear(s->stack, &s->stack_len);
        s->loop_sp = 0;
        s->for_hdr_sp = 0;
        s->sw.active = false;
        return IR2AEL_STATUS_HANDLED;
    }

    return IR2AEL_STATUS_NOT_HANDLED;
}

Ir2AelStatus ir2ael_handle_decl_ops(Ir2AelState *s, const IRInst *inst) {
    if (!s || !inst) return IR2AEL_STATUS_FAIL;
    if (inst->op != OP_ADD_GLOBAL && inst->op != OP_ADD_LOCAL) return IR2AEL_STATUS_NOT_HANDLED;

    if (inst->str) {
        bool is_local = (inst->op == OP_ADD_LOCAL);
        if (is_local && !s->in_function && !s->pending_defun && !s->global_local_block_open) {
            if (!ael_emit_at(s->out, s->out->line0, 0)) return IR2AEL_STATUS_FAIL_EMIT;
            if (!ael_emit_text(s->out, "{\n")) return IR2AEL_STATUS_FAIL_EMIT;
            s->global_local_block_open = true;
        }
        if (s->pending_decls.count == 0) {
            s->pending_decls.is_local = is_local;
            s->pending_decls.in_function = s->in_function || s->pending_defun;
            s->pending_decls.depth = s->cur_depth;
        } else if (s->pending_decls.is_local != is_local) {
            int flush_col0 = s->pending_decls.in_function ?
                decl_indent_col0_from_depth(s->pending_decls.depth > 0 ? s->pending_decls.depth : 1) : 0;
            if (!decl_group_emit_and_track(s->out, &s->pending_decls, s->out->line0, flush_col0, &s->local_init)) {
                return IR2AEL_STATUS_FAIL_EMIT;
            }
            decl_group_clear(&s->pending_decls);
            s->pending_decls.is_local = is_local;
            s->pending_decls.in_function = s->in_function || s->pending_defun;
            s->pending_decls.depth = s->cur_depth;
        }
        int max_decl_names = (int)(sizeof(s->pending_decls.names) / sizeof(s->pending_decls.names[0]));
        if (s->pending_decls.count >= max_decl_names) {
            int flush_col0 = s->pending_decls.in_function ?
                decl_indent_col0_from_depth(s->pending_decls.depth > 0 ? s->pending_decls.depth : 1) : 0;
            if (!decl_group_emit_and_track(s->out, &s->pending_decls, s->out->line0, flush_col0, &s->local_init)) {
                return IR2AEL_STATUS_FAIL_EMIT;
            }
            decl_group_clear(&s->pending_decls);
            s->pending_decls.is_local = is_local;
            s->pending_decls.in_function = s->in_function || s->pending_defun;
            s->pending_decls.depth = s->cur_depth;
        }
        if (s->pending_decls.count < max_decl_names) {
            strncpy(s->pending_decls.names[s->pending_decls.count], inst->str, 255);
            s->pending_decls.names[s->pending_decls.count][255] = '\0';
            s->pending_decls.count++;
        }
    }

    return IR2AEL_STATUS_HANDLED;
}
