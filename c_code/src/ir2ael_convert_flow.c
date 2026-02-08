/* ir2ael_convert_flow.c - control-flow dispatcher */
#include "ir2ael_internal.h"

Ir2AelStatus ir2ael_handle_flow_ops(Ir2AelState *s, size_t *idx, const IRInst *inst) {
    if (!s || !inst || !idx) return IR2AEL_STATUS_FAIL;

    Ir2AelStatus rc = ir2ael_flow_handle_switch_ops(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_begin_loop(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_end_loop(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_loop_ctrl(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_add_label(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_set_label(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_branch_true(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    rc = ir2ael_flow_handle_load_true(s, idx, inst);
    if (rc != IR2AEL_STATUS_NOT_HANDLED) return rc;

    return IR2AEL_STATUS_NOT_HANDLED;
}
