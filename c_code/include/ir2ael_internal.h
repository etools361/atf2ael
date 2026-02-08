#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ael_emit.h"
#include "ir_text_parser.h"
#include "ir_opcodes.h"

/* Extra IR opcodes used by control-flow templates (not all are in ir_opcodes.h yet). */
#define OP_BEGIN_LOOP 36
#define OP_END_LOOP 37
#define OP_LOOP_AGAIN 38
#define OP_LOOP_EXIT 39
#define OP_ADD_CASE 40
#define OP_BRANCH_TABLE 41
#define OP_SET_LOOP_DEFAULT 53
#define OP_LOAD_TRUE 7

typedef struct DeclGroup {
    char names[64][256];
    int count;
    bool is_local;
    bool in_function;
    int depth;
} DeclGroup;

typedef struct LocalInitEntry {
    char name[256];
    bool assigned;
    bool ambiguous;
} LocalInitEntry;

typedef struct LocalInitTracker {
    LocalInitEntry entries[256];
    int count;
} LocalInitTracker;

typedef enum ExprKind {
    EXPR_INT,
    EXPR_REAL,
    EXPR_IMAG,
    EXPR_STR,
    EXPR_BOOL,
    EXPR_NULL,
    EXPR_VAR,
    EXPR_LIST,
    EXPR_BINOP,
    EXPR_UNOP,
    EXPR_CALLARGS, /* internal marker */
    EXPR_CALL,
    EXPR_INDEX,
    EXPR_INCDEC,
    EXPR_TERNARY
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    int op_code;     /* for ops */
    int op_line0;    /* for ops */
    int op_col0;     /* for ops */
    unsigned int flags;
    struct Expr *lhs;
    struct Expr *mid; /* for ternary: then */
    struct Expr *rhs;
    int int_value;
    int bool_value;
    double num_value;
    char *text;      /* var name or string literal (raw) */

    /* list */
    struct Expr **items;
    int item_count;
    int close_line0;
    int close_col0;

    /* call (CALL/PUSH_ARGS) */
    int call_argc;
    int lparen_line0;
    int lparen_col0;
    struct Expr **call_args;
    int call_arg_count;

    /* index (postfix [..]) */
    struct Expr *index_base;
    struct Expr **index_items;
    int index_count;

    /* inc/dec */
    bool incdec_is_prefix;
    bool incdec_is_inc;
} Expr;

enum {
    EXPR_FLAG_LVALUE_DUP = 1u << 0,
    EXPR_FLAG_ADDR_OF = 1u << 1
};

typedef struct DeclInitChain {
    bool active;
    int line0;
    bool is_local;
    bool in_function;
    int depth;
} DeclInitChain;

typedef struct IfCtx {
    int else_label;
    int end_label; /* only set for if that has an else block */
    int stage;     /* 1=then, 2=else */
    bool brace_style;
    bool else_brace_style;
    int depth; /* IR DEPTH at the 'if' header */
} IfCtx;

typedef struct LoopCtx {
    int kind; /* 1=while, 2=do-while (for/switch later) */
    int depth; /* IR DEPTH at the loop header (brace body is depth+1) */
    int start_label;
    int continue_label;
    int end_label;
    int while_line0; /* do-while: 'while' keyword position */
    int while_col0;
    int cond_rparen_col0;
    int header_line0;
    int header_col0;
    bool header_emitted;
    bool body_has_brace;
} LoopCtx;

typedef struct ForHeaderCtx {
    int line0;
    int cond_start_col0;
    int stage; /* 1=need cond, 2=need update, 3=need close ')' */
} ForHeaderCtx;

typedef struct SwitchCtx {
    bool active;
    int line0; /* 'switch' line */
    int col0;  /* 'switch' column */
    int depth; /* switch statement depth (DEPTH at 'switch' keyword) */
    bool opened;
    int table_label; /* label id used for BRANCH_TABLE entry (SET_LABEL before OP_BRANCH_TABLE) */
    int end_label;
    bool has_end_label;
    int pending_case_kind; /* 0=none, 1=case, 2=default */
    int pending_case_value;
    bool pending_case_emitted;
    int last_break_line0;
} SwitchCtx;

typedef enum Ir2AelStatus {
    IR2AEL_STATUS_NOT_HANDLED = 0,
    IR2AEL_STATUS_HANDLED = 1,
    IR2AEL_STATUS_FAIL = -1,
    IR2AEL_STATUS_OOM = -2,
    IR2AEL_STATUS_FAIL_EMIT = -3
} Ir2AelStatus;

typedef struct Ir2AelState {
    const IRProgram *program;
    AelEmitter *out;
    char *err;
    size_t err_cap;

    DeclGroup pending_decls;
    DeclInitChain decl_init_chain;
    LocalInitTracker local_init;
    int current_defun_line0;

    bool pending_defun;
    int pending_defun_line0;
    char pending_defun_name[512];
    char pending_defun_params[32][256];
    int pending_defun_param_count;
    bool in_function;
    bool function_brace_open;
    bool global_local_block_open;

    Expr **stack;
    size_t stack_len;
    size_t stack_cap;

    IfCtx if_stack[64];
    int if_sp;
    bool pending_inline_else_if;
    int pending_inline_else_line0;
    int pending_inline_else_col0;

    LoopCtx loop_stack[64];
    int loop_sp;

    ForHeaderCtx for_hdr_stack[32];
    int for_hdr_sp;

    SwitchCtx sw;

    int cur_depth;
    int anon_depth_stack[64];
    int anon_depth_sp;
} Ir2AelState;

/* Helper function prototypes */
void local_init_clear(LocalInitTracker *t);
int local_init_find(const LocalInitTracker *t, const char *name);
void local_init_mark_decl(LocalInitTracker *t, const char *name);
void local_init_mark_assigned(LocalInitTracker *t, const char *name);
void local_init_track_decl_group(LocalInitTracker *t, const DeclGroup *g);
void decl_group_clear(DeclGroup *g);
bool decl_group_emit(AelEmitter *out, const DeclGroup *g, int line0, int col0);
bool decl_group_emit_and_track(AelEmitter *out, const DeclGroup *g, int line0, int col0, LocalInitTracker *t);
size_t ir_skip_locals_bookkeeping(const IRProgram *program, size_t idx);
size_t ir_skip_locals_bookkeeping_back(const IRProgram *program, size_t idx);
void expr_free(Expr *e);
Expr *expr_new(ExprKind kind);
Expr *expr_clone(const Expr *e);
void expr_mark_addr_of(Expr *e, bool allow);
const char *op_code_to_str(int op_code);
bool ir_inst_is_scope_bookkeeping(const IRInst *inst);
size_t ir_skip_scope_bookkeeping(const IRProgram *program, size_t i);
size_t ir_skip_scope_bookkeeping_end(const IRProgram *program, size_t i, size_t end);
int op_precedence(int op_code);
bool binop_rhs_force_paren(int op_code, const Expr *rhs);
bool binop_lhs_force_paren(int op_code, const Expr *lhs);
bool emit_escaped_string(AelEmitter *out, const char *raw);
const char *unit_suffix_from_multiplier(double v);
void normalize_sci_literal(char *s);
void format_real_token(double v, char *out, size_t out_cap);
void format_imag_token(double v, char *out, size_t out_cap);
int decl_indent_col0_from_depth(int depth);
bool for_header_has_comma_op(const IRProgram *program, size_t i, int line0);
bool has_next_decl_init_on_same_line(const IRProgram *program, size_t cur_i, int line0);
bool else_body_has_brace_block(const IRProgram *program, size_t start, int end_label, int if_depth);
bool stmt_emit_at_or_current(AelEmitter *out, int *line0, int *col0, int depth);
int anon_block_current_depth(const int *stack, int sp);
bool anon_block_open_to(AelEmitter *out, int *sp, int *stack, int target_depth, int brace_line0);
bool anon_block_close_to(AelEmitter *out, int *sp, int *stack, int target_depth, int anchor_line0);
bool anon_depth_push_marked(int *sp, int *stack, int depth, bool is_controlflow);
void anon_depth_pop_expected(int *sp, const int *stack, int expected_depth, bool is_controlflow);
bool anon_scope_close_to(AelEmitter *out, int *sp, int *stack, int target_depth, int anchor_line0);
bool anon_close_scopes_before_stmt(AelEmitter *out, int *sp, int *stack, int target_depth, int stmt_line0);
bool anon_close_scope_blocks_at_function_end(AelEmitter *out, int *sp, int *stack, int target_depth, int anchor_line0);
int expr_start_col0(const Expr *e, int line0);
int expr_min_line0(const Expr *e);
void expr_min_col0_on_line_rec(const Expr *e, int line0, int *best_col0);
int expr_min_col0_on_line(const Expr *e, int line0);
int if_keyword_col0_from_cond(const Expr *cond, int if_line0, bool in_function);
int if_lparen_col0_from_cond(const Expr *cond, int if_line0, int if_col0);
bool emit_if_keyword_and_lparen(AelEmitter *out, int line0, int if_col0, int lparen_col0);
Expr *unwrap_logical_not(Expr *e);
bool ael_emit_at_expr_soft(AelEmitter *out, int line0, int col0);
bool expr_starts_with_unop_code(const Expr *e, int op_code);
bool ir_inst_is_load_trueish(const IRInst *inst);
bool emit_expr(AelEmitter *out, const Expr *e, int parent_prec);
bool emit_expr_addr(AelEmitter *out, Expr *e, int parent_prec);
bool stack_push(Expr ***stk, size_t *len, size_t *cap, Expr *e);
Expr *stack_pop(Expr **stk, size_t *len);
void stack_clear(Expr **stk, size_t *len);
Expr *stack_pop_stmt_expr(Expr **stk, size_t *len);
bool parse_expr_range(const IRProgram *program, size_t start, size_t end, Expr **out_expr, char *err, size_t err_cap);
bool if_close_braced_else_on_depth_exit(AelEmitter *out, IfCtx *if_stack, int *if_sp, int cur_depth, int *anon_sp, int *anon_stack);
bool ensure_switch_opened(AelEmitter *out, SwitchCtx *sw, int first_case_line0, int *anon_sp, int *anon_stack);
bool looks_like_inline_break_after_stmt_end(const IRProgram *program, size_t stmt_end_i, int line0);
bool op0_is_short_circuit_marker(const IRProgram *program, size_t idx, size_t end);
bool begin_loop_has_for_scaffold(const IRProgram *program, size_t begin_i, int start_label);
bool switch_emit_pending_case_before_stmt(AelEmitter *out, SwitchCtx *sw, int stmt_line0, int stmt_col0, int *anon_sp, int *anon_stack);
bool switch_emit_pending_case_label_only(AelEmitter *out, SwitchCtx *sw, int *anon_sp, int *anon_stack);
bool switch_is_epilogue_branch(const IRProgram *program, size_t branch_i, const SwitchCtx *sw);
bool scan_for_if_header_line(const IRProgram *program, size_t start, size_t max_scan, int line0);
bool scan_for_if_header_any(const IRProgram *program, size_t start, size_t max_scan, int *out_line0);
bool scan_for_if_header_until_label(const IRProgram *program, size_t start, size_t max_scan, int stop_label, int expected_depth, int *out_line0);
bool ir_if_header_at(const IRProgram *program, size_t i, size_t end, int expected_depth, int *out_line0);
bool ir_else_if_chain_at(const IRProgram *program, size_t i, size_t end, int expected_depth, int outer_end_label, int *out_line0);
bool num_local_should_open_scope_block(const IRProgram *program, size_t i, int target_depth);
bool scan_for_assignment_to_var(const IRProgram *program, size_t start, size_t max_scan, const char *var_name, int depth, bool strict_depth);
bool find_for_header_cond_col0(const IRProgram *program, size_t start, size_t max_scan, int line0, int *out_col0);
bool find_for_header_lparen_col0(const IRProgram *program, size_t start, size_t max_scan, int line0, int incr_label, int loop_end_label, int *out_col0);

void ir2ael_state_init(Ir2AelState *s, const IRProgram *program, AelEmitter *out, char *err, size_t err_cap);
void ir2ael_state_free(Ir2AelState *s);
Ir2AelStatus ir2ael_preprocess_inst(Ir2AelState *s, size_t i, const IRInst *inst);
Ir2AelStatus ir2ael_handle_function_ops(Ir2AelState *s, size_t i, const IRInst *inst);
Ir2AelStatus ir2ael_handle_decl_ops(Ir2AelState *s, const IRInst *inst);
Ir2AelStatus ir2ael_handle_scope_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_handle_load_ops(Ir2AelState *s, const IRInst *inst);
Ir2AelStatus ir2ael_handle_flow_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_handle_expr_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_finalize(Ir2AelState *s);

Ir2AelStatus ir2ael_flow_handle_switch_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_begin_loop(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_end_loop(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_loop_ctrl(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_add_label(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_set_label(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_branch_true(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_flow_handle_load_true(Ir2AelState *s, size_t *i, const IRInst *inst);

Ir2AelStatus ir2ael_expr_handle_assign_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_expr_handle_list_call_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
Ir2AelStatus ir2ael_expr_handle_misc_ops(Ir2AelState *s, size_t *i, const IRInst *inst);
