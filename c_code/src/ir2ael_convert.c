/* ir2ael_convert.c - main IR->AEL conversion loop */
#include "ir2ael_convert.h"
#include "ir2ael_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool ir2ael_convert_program(const IRProgram *program, AelEmitter *out, char *err, size_t err_cap) {
    if (err && err_cap) err[0] = '\0';
    if (!program || !out) return false;

    Ir2AelState st;
    ir2ael_state_init(&st, program, out, err, err_cap);
    Ir2AelStatus rc = IR2AEL_STATUS_NOT_HANDLED;

    size_t i = 0;
    const IRInst *inst = NULL;
    for (; i < program->count; i++) {
        inst = &program->insts[i];
        rc = ir2ael_preprocess_inst(&st, i, inst);
        if (rc < 0) goto fail_by_rc;

        rc = ir2ael_handle_function_ops(&st, i, inst);
        if (rc < 0) goto fail_by_rc;
        if (rc > 0) continue;

        rc = ir2ael_handle_decl_ops(&st, inst);
        if (rc < 0) goto fail_by_rc;
        if (rc > 0) continue;

        rc = ir2ael_handle_scope_ops(&st, &i, inst);
        if (rc < 0) goto fail_by_rc;
        if (rc > 0) continue;

        rc = ir2ael_handle_load_ops(&st, inst);
        if (rc < 0) goto fail_by_rc;
        if (rc > 0) continue;

        rc = ir2ael_handle_flow_ops(&st, &i, inst);
        if (rc < 0) goto fail_by_rc;
        if (rc > 0) continue;

        rc = ir2ael_handle_expr_ops(&st, &i, inst);
        if (rc < 0) goto fail_by_rc;
        if (rc > 0) continue;

        /* If we have a pending decl group with no initializer, emit it before moving on. */
        if (st.pending_decls.count > 0) {
            int decl_line0 = out->line0;
            int decl_col0 = 0;
            if (st.pending_decls.is_local && (st.in_function || st.pending_defun) && st.current_defun_line0 >= 0) {
                if (st.pending_decls.depth <= 1) {
                    decl_line0 = st.current_defun_line0 + 2;
                }
                decl_col0 = decl_indent_col0_from_depth(st.pending_decls.depth > 0 ? st.pending_decls.depth : 1);
            }
            if (!decl_group_emit_and_track(out, &st.pending_decls, decl_line0, decl_col0, &st.local_init)) goto fail_emit;
            decl_group_clear(&st.pending_decls);
        }

        /* Ignore DEPTH, comments already filtered; anything else is unsupported in M1/M2 subset. */
        if (err && err_cap) snprintf(err, err_cap, "unsupported opcode OP=%d at IR index %zu", inst->op, i);
        goto fail;
    }

    rc = ir2ael_finalize(&st);
    if (rc < 0) goto fail_by_rc;
    ir2ael_state_free(&st);
    return true;

fail_by_rc:
    if (rc == IR2AEL_STATUS_OOM) goto oom;
    if (rc == IR2AEL_STATUS_FAIL_EMIT) goto fail_emit;
    goto fail;

oom:
    if (err && err_cap) snprintf(err, err_cap, "out of memory");
fail:
    ir2ael_state_free(&st);
    return false;

fail_emit:
    if (err && err_cap) {
        const char *reason = (out && out->last_fail_reason == AEL_EMIT_FAIL_BACKWARD_LINE) ? "backward_line" :
                             (out && out->last_fail_reason == AEL_EMIT_FAIL_BACKWARD_COL) ? "backward_col" :
                             (out && out->last_fail_reason == AEL_EMIT_FAIL_IO) ? "io" : "unknown";
        if (inst) {
            snprintf(err, err_cap,
                     "emit failed at IR index %zu (OP=%d arg1=%d arg2=%d arg3=%d) at out=%d:%d req=%d:%d (%s)",
                     i, inst->op,
                     inst->has_arg1 ? inst->arg1 : 0,
                     inst->has_arg2 ? inst->arg2 : 0,
                     inst->has_arg3 ? inst->arg3 : 0,
                     out ? out->line0 : -1, out ? out->col0 : -1,
                     out ? out->last_req_line0 : -1, out ? out->last_req_col0 : -1,
                     reason);
        } else {
            snprintf(err, err_cap,
                     "emit failed at IR index %zu at out=%d:%d req=%d:%d (%s)",
                     i,
                     out ? out->line0 : -1, out ? out->col0 : -1,
                     out ? out->last_req_line0 : -1, out ? out->last_req_col0 : -1,
                     reason);
        }
    }
    goto fail;
}
