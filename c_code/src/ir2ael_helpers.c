/* ir2ael_helpers.c - helper routines for IR->AEL conversion */
#include "ir2ael_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>




void local_init_clear(LocalInitTracker *t) {
    if (!t) return;
    t->count = 0;
}

int local_init_find(const LocalInitTracker *t, const char *name) {
    if (!t || !name || !*name) return -1;
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->entries[i].name, name) == 0) return i;
    }
    return -1;
}

void local_init_mark_decl(LocalInitTracker *t, const char *name) {
    if (!t || !name || !*name) return;
    int idx = local_init_find(t, name);
    if (idx >= 0) {
        t->entries[idx].ambiguous = true;
        return;
    }
    if (t->count >= (int)(sizeof(t->entries) / sizeof(t->entries[0]))) return;
    strncpy(t->entries[t->count].name, name, sizeof(t->entries[t->count].name) - 1);
    t->entries[t->count].name[sizeof(t->entries[t->count].name) - 1] = '\0';
    t->entries[t->count].assigned = false;
    t->entries[t->count].ambiguous = false;
    t->count++;
}

void local_init_mark_assigned(LocalInitTracker *t, const char *name) {
    if (!t || !name || !*name) return;
    int idx = local_init_find(t, name);
    if (idx < 0) return;
    t->entries[idx].assigned = true;
}

void local_init_track_decl_group(LocalInitTracker *t, const DeclGroup *g) {
    if (!t || !g || !g->is_local || !g->in_function) return;
    for (int i = 0; i < g->count; i++) {
        local_init_mark_decl(t, g->names[i]);
    }
}

void decl_group_clear(DeclGroup *g) {
    if (!g) return;
    g->count = 0;
    g->is_local = false;
    g->in_function = false;
    g->depth = 0;
}

bool decl_group_emit(AelEmitter *out, const DeclGroup *g, int line0, int col0) {
    if (!g || g->count <= 0) return true;
    if (out && !out->strict_pos) {
        /* When strict_pos is disabled, ael_emit_at() becomes a no-op; make sure decls don't get
           concatenated onto the previous statement. */
        if (out->col0 != 0) {
            if (!ael_emit_char(out, '\n')) return false;
        }
        for (int i = 0; i < col0; i++) {
            if (!ael_emit_char(out, ' ')) return false;
        }
    }
    if (!ael_emit_at(out, line0, col0)) return false;
    if (!ael_emit_text(out, "decl ")) return false;
    for (int i = 0; i < g->count; i++) {
        if (i != 0) {
            if (!ael_emit_text(out, ", ")) return false;
        }
        if (!ael_emit_text(out, g->names[i])) return false;
    }
    return ael_emit_text(out, ";\n");
}

bool decl_group_emit_and_track(AelEmitter *out, const DeclGroup *g, int line0, int col0, LocalInitTracker *t) {
    if (!decl_group_emit(out, g, line0, col0)) return false;
    local_init_track_decl_group(t, g);
    return true;
}

/* Some control-flow templates can have extra NUM_LOCAL/DROP_LOCAL bookkeeping inserted
 * between key ops (e.g. before break/continue). In non-strict mode we should ignore
 * these so template recognition stays stable for ATF-derived IR. */
size_t ir_skip_locals_bookkeeping(const IRProgram *program, size_t idx) {
    if (!program) return idx;
    while (idx < program->count) {
        int op = program->insts[idx].op;
        if (op == OP_NUM_LOCAL || op == OP_DROP_LOCAL) {
            idx++;
            continue;
        }
        break;
    }
    return idx;
}

size_t ir_skip_locals_bookkeeping_back(const IRProgram *program, size_t idx) {
    if (!program) return idx;
    while (idx > 0) {
        int op = program->insts[idx - 1].op;
        if (op == OP_NUM_LOCAL || op == OP_DROP_LOCAL) {
            idx--;
            continue;
        }
        break;
    }
    return idx;
}




void expr_free(Expr *e) {
    if (!e) return;
    expr_free(e->lhs);
    expr_free(e->mid);
    expr_free(e->rhs);
    expr_free(e->index_base);
    if (e->items) {
        for (int i = 0; i < e->item_count; i++) expr_free(e->items[i]);
        free(e->items);
    }
    if (e->call_args) {
        for (int i = 0; i < e->call_arg_count; i++) expr_free(e->call_args[i]);
        free(e->call_args);
    }
    if (e->index_items) {
        for (int i = 0; i < e->index_count; i++) expr_free(e->index_items[i]);
        free(e->index_items);
    }
    free(e->text);
    free(e);
}

Expr *expr_new(ExprKind kind) {
    Expr *e = (Expr *)calloc(1, sizeof(Expr));
    if (!e) return NULL;
    e->kind = kind;
    e->op_line0 = -1;
    e->op_col0 = -1;
    e->close_line0 = -1;
    e->close_col0 = -1;
    e->lparen_line0 = -1;
    e->lparen_col0 = -1;
    return e;
}

Expr *expr_clone(const Expr *e) {
    if (!e) return NULL;
    Expr *c = expr_new(e->kind);
    if (!c) return NULL;
    c->op_code = e->op_code;
    c->op_line0 = e->op_line0;
    c->op_col0 = e->op_col0;
    c->flags = e->flags;
    c->int_value = e->int_value;
    c->num_value = e->num_value;
    if (e->text) {
        c->text = _strdup(e->text);
        if (!c->text) {
            expr_free(c);
            return NULL;
        }
    }
    c->close_line0 = e->close_line0;
    c->close_col0 = e->close_col0;
    c->call_argc = e->call_argc;
    c->lparen_line0 = e->lparen_line0;
    c->lparen_col0 = e->lparen_col0;
    c->incdec_is_prefix = e->incdec_is_prefix;
    c->incdec_is_inc = e->incdec_is_inc;

    if (e->lhs) {
        c->lhs = expr_clone(e->lhs);
        if (!c->lhs) {
            expr_free(c);
            return NULL;
        }
    }
    if (e->mid) {
        c->mid = expr_clone(e->mid);
        if (!c->mid) {
            expr_free(c);
            return NULL;
        }
    }
    if (e->rhs) {
        c->rhs = expr_clone(e->rhs);
        if (!c->rhs) {
            expr_free(c);
            return NULL;
        }
    }
    if (e->items && e->item_count > 0) {
        c->items = (Expr **)calloc((size_t)e->item_count, sizeof(Expr *));
        if (!c->items) {
            expr_free(c);
            return NULL;
        }
        c->item_count = e->item_count;
        for (int i = 0; i < e->item_count; i++) {
            c->items[i] = expr_clone(e->items[i]);
            if (!c->items[i]) {
                expr_free(c);
                return NULL;
            }
        }
    }
    if (e->call_args && e->call_arg_count > 0) {
        c->call_args = (Expr **)calloc((size_t)e->call_arg_count, sizeof(Expr *));
        if (!c->call_args) {
            expr_free(c);
            return NULL;
        }
        c->call_arg_count = e->call_arg_count;
        for (int i = 0; i < e->call_arg_count; i++) {
            c->call_args[i] = expr_clone(e->call_args[i]);
            if (!c->call_args[i]) {
                expr_free(c);
                return NULL;
            }
        }
    }
    if (e->index_base) {
        c->index_base = expr_clone(e->index_base);
        if (!c->index_base) {
            expr_free(c);
            return NULL;
        }
    }
    if (e->index_items && e->index_count > 0) {
        c->index_items = (Expr **)calloc((size_t)e->index_count, sizeof(Expr *));
        if (!c->index_items) {
            expr_free(c);
            return NULL;
        }
        c->index_count = e->index_count;
        for (int i = 0; i < e->index_count; i++) {
            c->index_items[i] = expr_clone(e->index_items[i]);
            if (!c->index_items[i]) {
                expr_free(c);
                return NULL;
            }
        }
    }
    return c;
}

void expr_mark_addr_of(Expr *e, bool allow) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_VAR:
            if (allow && e->op_line0 < 0 && e->op_col0 < 0) {
                e->flags |= EXPR_FLAG_ADDR_OF;
            } else {
                e->flags &= ~EXPR_FLAG_ADDR_OF;
            }
            return;
        case EXPR_LIST:
            for (int i = 0; i < e->item_count; i++) {
                expr_mark_addr_of(e->items[i], allow);
            }
            return;
        case EXPR_CALL:
            expr_mark_addr_of(e->lhs, false);
            for (int i = 0; i < e->call_arg_count; i++) {
                expr_mark_addr_of(e->call_args[i], true);
            }
            return;
        case EXPR_INDEX:
            expr_mark_addr_of(e->index_base, false);
            for (int i = 0; i < e->index_count; i++) {
                expr_mark_addr_of(e->index_items[i], true);
            }
            return;
        case EXPR_INCDEC:
            expr_mark_addr_of(e->lhs, false);
            return;
        case EXPR_UNOP:
            expr_mark_addr_of(e->rhs, allow);
            return;
        case EXPR_BINOP:
            if (e->op_code == 16) {
                /* Never infer address-of on assignment lvalue. */
                expr_mark_addr_of(e->lhs, false);
                expr_mark_addr_of(e->rhs, allow);
            } else {
                expr_mark_addr_of(e->lhs, allow);
                expr_mark_addr_of(e->rhs, allow);
            }
            return;
        case EXPR_TERNARY:
            expr_mark_addr_of(e->lhs, allow);
            expr_mark_addr_of(e->mid, allow);
            expr_mark_addr_of(e->rhs, allow);
            return;
        default:
            return;
    }
}

const char *op_code_to_str(int op_code) {
    switch (op_code) {
        case 16: return "=";  /* assignment expression */
        case 10: return "+";
        case 11: return "-";
        case 12: return "*";
        case 13: return "%";
        case 14: return "/";
        case 43: return "**";
        case 47: return ",";  /* comma expression */
        case 4: return "==";
        case 5: return "!=";
        case 6: return ">=";
        case 7: return "<=";
        case 8: return ">";
        case 9: return "<";
        case 18: return "&&";
        case 19: return "||";
        case 25: return "&";
        case 26: return "^";
        case 27: return "|";
        case 29: return "<<";
        case 30: return ">>";
        case 3: return "!";   /* unary NOT (in parser: acomp_op(3,...)) */
        case 15: return "-";  /* unary negate */
        default: return NULL;
    }
}

bool ir_inst_is_scope_bookkeeping(const IRInst *inst) {
    if (!inst) return false;
    /* Locals/scope bookkeeping (emitter-dependent). */
    return inst->op == OP_NUM_LOCAL || inst->op == OP_DROP_LOCAL;
}

size_t ir_skip_scope_bookkeeping(const IRProgram *program, size_t i) {
    if (!program) return i;
    while (i < program->count && ir_inst_is_scope_bookkeeping(&program->insts[i])) {
        i++;
    }
    return i;
}

size_t ir_skip_scope_bookkeeping_end(const IRProgram *program, size_t i, size_t end) {
    if (!program) return i;
    if (end > program->count) end = program->count;
    while (i < end && ir_inst_is_scope_bookkeeping(&program->insts[i])) {
        i++;
    }
    return i;
}

int op_precedence(int op_code) {
    switch (op_code) {
        case 16: return 0;  /* assignment: special-cased for parens */
        case 47: return 0;  /* comma: special-cased for parens */
        case 19: return 1;  /* || */
        case 18: return 2;  /* && */
        case 27: return 3;  /* | */
        case 26: return 4;  /* ^ */
        case 25: return 5;  /* & */
        case 4:
        case 5: return 6;   /* == != */
        case 6:
        case 7:
        case 8:
        case 9: return 7;   /* comparisons */
        case 29:
        case 30: return 8;  /* shifts */
        case 10:
        case 11: return 9;  /* + - */
        case 12:
        case 13:
        case 14: return 10; /* * / % */
        case 3:
        case 15: return 11; /* unary */
        case 43: return 12; /* power (**) */
        default: return 0;
    }
}

bool binop_rhs_force_paren(int op_code, const Expr *rhs) {
    if (!rhs || rhs->kind != EXPR_BINOP) return false;
    int prec = op_precedence(op_code);
    if (prec <= 0) return false;
    int rprec = op_precedence(rhs->op_code);
    if (rprec != prec) return false;

    /* Preserve explicit IR tree structure when pretty-printing:
       for many left-associative ops, nesting on the RHS changes meaning unless parenthesized. */
    switch (op_code) {
        case 11: /* - */
        case 14: /* / */
        case 13: /* % */
        case 29: /* << */
        case 30: /* >> */
        case 4:  /* == */
        case 5:  /* != */
        case 6:  /* >= */
        case 7:  /* <= */
        case 8:  /* > */
        case 9:  /* < */
        case 43: /* ** (treat as non-associative here) */
            return true;
        case 10: /* + */
        case 12: /* * */
            /* Keep explicit RHS grouping from IR (e.g. a + (b + c)); otherwise parsing flattens to left-assoc. */
            return true;
        case 18: /* && */
        case 19: /* || */
        case 25: /* & */
        case 26: /* ^ */
        case 27: /* | */
            /* For these, avoid re-association unless the operator actually differs. */
            return rhs->op_code != op_code;
        default:
            return true;
    }
}

bool binop_lhs_force_paren(int op_code, const Expr *lhs) {
    if (!lhs || lhs->kind != EXPR_BINOP) return false;
    int prec = op_precedence(op_code);
    if (prec <= 0) return false;
    int lprec = op_precedence(lhs->op_code);
    if (lprec != prec) return false;

    /* Power is right-associative; preserve explicit left-nesting when present. */
    if (op_code == 43) return true;
    return false;
}

bool emit_escaped_string(AelEmitter *out, const char *raw) {
    if (!ael_emit_char(out, '"')) return false;
    /*
     * AEL lexer preserves escape sequences inside strings (it stores the '\' and following char as-is).
     * To match baseline IR, we generally want to emit the raw bytes without re-escaping (especially
     * backslashes like "\View"), otherwise token columns shift and roundtrip fails.
     */
    const char *s = raw ? raw : "";
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == '"' && (p == s || p[-1] != '\\')) {
            /* Best-effort: if an unescaped quote appears, escape it so the file stays parseable. */
            if (!ael_emit_text(out, "\\\"")) return false;
            continue;
        }
        if (!ael_emit_char(out, c)) return false;
    }
    return ael_emit_char(out, '"');
}

const char *unit_suffix_from_multiplier(double v) {
    typedef struct {
        const char *name;
        double mult;
    } Unit;
    static const Unit units[] = {
        {"um", 1e-6},
        {"mm", 1e-3},
        {NULL, 0.0}
    };

    double av = (v < 0) ? -v : v;
    for (int i = 0; units[i].name; i++) {
        double diff = v - units[i].mult;
        if (diff < 0) diff = -diff;
        double denom = (av > 1.0) ? av : 1.0;
        if (diff / denom < 1e-12) return units[i].name;
    }
    return NULL;
}

void normalize_sci_literal(char *s) {
    if (!s) return;
    char *e = strchr(s, 'e');
    if (!e) e = strchr(s, 'E');
    if (!e) return;

    *e = 'e';
    char *p = e + 1;
    if (*p == '+') {
        memmove(p, p + 1, strlen(p + 1) + 1);
    }
    if (*p == '-') p++;

    while (p[0] == '0' && isdigit((unsigned char)p[1])) {
        memmove(p, p + 1, strlen(p + 1) + 1);
    }
}

void format_real_token(double v, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;

    char best[128];
    best[0] = '\0';
    size_t best_len = (size_t)-1;

    /* Try to find a short decimal/scientific literal that round-trips via strtod. */
    for (int prec = 1; prec <= 17; prec++) {
        char cand[128];
        cand[0] = '\0';

        /* %g */
        snprintf(cand, sizeof(cand), "%.*g", prec, v);
        normalize_sci_literal(cand);
        if (!strchr(cand, '.') && !strchr(cand, 'e') && !strchr(cand, 'E')) {
            strncat(cand, ".0", sizeof(cand) - strlen(cand) - 1);
        }
        if (strtod(cand, NULL) == v) {
            size_t n = strlen(cand);
            if (n < best_len) {
                strncpy(best, cand, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                best_len = n;
            }
        }

        /* %e */
        snprintf(cand, sizeof(cand), "%.*e", prec, v);
        normalize_sci_literal(cand);
        if (!strchr(cand, '.') && !strchr(cand, 'e') && !strchr(cand, 'E')) {
            strncat(cand, ".0", sizeof(cand) - strlen(cand) - 1);
        }
        if (strtod(cand, NULL) == v) {
            size_t n = strlen(cand);
            if (n < best_len) {
                strncpy(best, cand, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                best_len = n;
            }
        }
    }

    if (best[0] == '\0') {
        snprintf(best, sizeof(best), "%.17g", v);
        normalize_sci_literal(best);
        if (!strchr(best, '.') && !strchr(best, 'e') && !strchr(best, 'E')) {
            strncat(best, ".0", sizeof(best) - strlen(best) - 1);
        }
    }

    strncpy(out, best, out_cap - 1);
    out[out_cap - 1] = '\0';
}

void format_imag_token(double v, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    char best[128];
    best[0] = '\0';
    size_t best_len = (size_t)-1;
    for (int prec = 1; prec <= 17; prec++) {
        char cand[128];
        cand[0] = '\0';

        snprintf(cand, sizeof(cand), "%.*g", prec, v);
        if (strtod(cand, NULL) == v) {
            size_t n = strlen(cand);
            if (n < best_len) {
                strncpy(best, cand, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                best_len = n;
            }
        }

        snprintf(cand, sizeof(cand), "%.*e", prec, v);
        if (strtod(cand, NULL) == v) {
            size_t n = strlen(cand);
            if (n < best_len) {
                strncpy(best, cand, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                best_len = n;
            }
        }
    }
    if (best[0] == '\0') {
        snprintf(best, sizeof(best), "%.17g", v);
    }
    strncpy(out, best, out_cap - 1);
    out[out_cap - 1] = '\0';
}

int decl_indent_col0_from_depth(int depth) {
    if (depth <= 0) return 0;
    return depth * 4;
}

bool for_header_has_comma_op(const IRProgram *program, size_t i, int line0) {
    if (!program) return false;
    size_t end = i + 96;
    if (end > program->count) end = program->count;
    for (size_t j = i + 1; j < end; j++) {
        const IRInst *mj = &program->insts[j];
        if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
        if (mj->op == OP_OP && mj->has_arg1 && mj->arg1 == 47 &&
            mj->has_arg2 && mj->arg2 == line0) {
            return true;
        }
        if (mj->op == OP_OP && mj->has_arg1 && mj->arg1 == 0 &&
            mj->has_arg2 && mj->arg2 == line0) {
            break;
        }
        if (mj->op == OP_LOAD_TRUE &&
            j + 1 < program->count && program->insts[j + 1].op == OP_BRANCH_TRUE &&
            program->insts[j + 1].has_arg2 && program->insts[j + 1].arg2 == line0) {
            break;
        }
    }
    return false;
}


bool has_next_decl_init_on_same_line(const IRProgram *program, size_t cur_i, int line0) {
    bool saw_add = false;
    for (size_t j = cur_i + 1; j < program->count; j++) {
        const IRInst *n = &program->insts[j];
        if (n->op == OP_ADD_LOCAL || n->op == OP_ADD_GLOBAL) {
            saw_add = true;
            continue;
        }
        if (n->op == OP_OP && n->has_arg1 && n->arg1 == 16 && n->has_arg2 && n->arg2 == line0) {
            return saw_add;
        }
        if (n->has_arg2 && n->arg2 > line0) return false;
        if (n->op == OP_BEGIN_FUNCT || n->op == OP_DEFINE_FUNCT) return false;
    }
    return false;
}

bool else_body_has_brace_block(const IRProgram *program, size_t start, int end_label, int if_depth) {
    if (!program) return false;
    const size_t max_scan = 16;
    for (size_t j = start; j < program->count && j < start + max_scan; j++) {
        const IRInst *mj = &program->insts[j];
        if (mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == end_label) return false;
        if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) return false;
        if (mj->op == OP_NUM_LOCAL) {
            /* Braced blocks begin with NUM_LOCAL at one deeper lexical depth. */
            if (mj->has_depth && mj->depth == if_depth + 1) return true;
            return false;
        }
        /* Any other opcode means the else-body has started without a leading NUM_LOCAL. */
        return false;
    }
    return false;
}

bool stmt_emit_at_or_current(AelEmitter *out, int *line0, int *col0, int depth) {
    if (!out || !line0 || !col0) return false;
    if (*line0 < out->line0) {
        *line0 = out->line0;
        *col0 = decl_indent_col0_from_depth(depth);
    } else if (*line0 == out->line0 && *col0 < out->col0) {
        *col0 = out->col0;
    }
    return ael_emit_at(out, *line0, *col0);
}

int anon_block_current_depth(const int *stack, int sp) {
    if (sp <= 0) return 1;
    int v = stack[sp - 1];
    return (v < 0) ? -v : v;
}

bool anon_block_open_to(AelEmitter *out, int *sp, int *stack, int target_depth, int brace_line0) {
    if (!out || !sp || !stack) return false;
    if (target_depth <= 1) return true;

    int cur = anon_block_current_depth(stack, *sp);
    int line0 = brace_line0;
    while (cur < target_depth) {
        int new_depth = cur + 1;
        int col0 = decl_indent_col0_from_depth(new_depth - 1);
        if (line0 < 0) line0 = 0;
        if (!ael_emit_at(out, line0, col0)) return false;
        if (!ael_emit_text(out, "{\n")) return false;
        if (*sp < 64) {
            stack[(*sp)++] = new_depth;
        }
        cur = new_depth;
        line0++;
    }
    return true;
}

bool anon_block_close_to(AelEmitter *out, int *sp, int *stack, int target_depth, int anchor_line0) {
    if (!out || !sp || !stack) return false;
    if (target_depth < 1) target_depth = 1;

    int cur = anon_block_current_depth(stack, *sp);
    if (cur <= target_depth) return true;

    /* Emit multiple closing braces in increasing line order to avoid backward seeks. */
    int n = cur - target_depth;
    int first_line0 = anchor_line0 - n;
    if (first_line0 < 0) first_line0 = 0;
    for (int k = 0; k < n; k++) {
        int depth_to_close = cur - k;
        int col0 = decl_indent_col0_from_depth(depth_to_close - 1);
        int line0 = first_line0 + k;
        if (!ael_emit_at(out, line0, col0)) return false;
        if (!ael_emit_text(out, "}\n")) return false;
        if (*sp > 0) (*sp)--;
    }
    return true;
}

bool anon_depth_push_marked(int *sp, int *stack, int depth, bool is_controlflow) {
    if (!sp || !stack) return false;
    if (*sp >= 64) return false;
    int v = (depth < 0) ? -depth : depth;
    stack[(*sp)++] = is_controlflow ? -v : v;
    return true;
}

void anon_depth_pop_expected(int *sp, const int *stack, int expected_depth, bool is_controlflow) {
    if (!sp || !stack) return;
    if (*sp <= 0) return;
    int want = (expected_depth < 0) ? -expected_depth : expected_depth;
    int expected = is_controlflow ? -want : want;
    if (stack[*sp - 1] == expected) (*sp)--;
}

bool anon_scope_close_to(AelEmitter *out, int *sp, int *stack, int target_depth, int anchor_line0) {
    if (!out || !sp || !stack) return false;
    if (target_depth < 1) target_depth = 1;

    /* Count how many positive (scope) braces are above target_depth. Stop at controlflow brace markers. */
    int n = 0;
    for (int k = *sp - 1; k >= 0; k--) {
        int v = stack[k];
        if (v < 0) break;
        if (v <= target_depth) break;
        n++;
    }
    if (n <= 0) return true;

    int first_line0 = anchor_line0 - n;
    if (first_line0 < out->line0) first_line0 = out->line0;
    if (first_line0 < 0) first_line0 = 0;

    for (int i = 0; i < n; i++) {
        int idx = (*sp) - 1;
        if (idx < 0) break;
        int depth_to_close = stack[idx];
        if (depth_to_close < 0) break;
        if (depth_to_close <= target_depth) break;

        int col0 = decl_indent_col0_from_depth(depth_to_close - 1);
        int line0 = first_line0 + i;
        if (!ael_emit_at(out, line0, col0)) return false;
        if (!ael_emit_text(out, "}\n")) return false;
        (*sp)--;
    }
    return true;
}

bool anon_close_scopes_before_stmt(AelEmitter *out, int *sp, int *stack, int target_depth, int stmt_line0) {
    return anon_scope_close_to(out, sp, stack, target_depth, stmt_line0);
}

/* At function end, close any remaining scope-only blocks we opened for locals reconstruction.
   Ignore stale control-flow markers (negative depths) to avoid synthesizing stray '}' between functions. */
bool anon_close_scope_blocks_at_function_end(AelEmitter *out, int *sp, int *stack, int target_depth, int anchor_line0) {
    if (!out || !sp || !stack) return false;
    if (target_depth < 1) target_depth = 1;

    int line0 = anchor_line0;
    if (line0 < out->line0) line0 = out->line0;
    if (line0 < 0) line0 = 0;

    while (*sp > 0) {
        int v = stack[*sp - 1];
        int depth = (v < 0) ? -v : v;
        if (depth <= target_depth) break;
        (*sp)--;
        if (v < 0) continue; /* discard controlflow marker */
        int col0 = decl_indent_col0_from_depth(depth - 1);
        if (!ael_emit_at(out, line0, col0)) return false;
        if (!ael_emit_text(out, "}\n")) return false;
        line0++;
    }
    return true;
}

int expr_start_col0(const Expr *e, int line0) {
    if (!e) return 0;
    if (e->op_line0 != line0) return 0;
    if (e->kind == EXPR_BINOP && e->op_code == 47) {
        int lhs_col0 = expr_start_col0(e->lhs, line0);
        if (lhs_col0 > 0) return lhs_col0;
    }
    if (e->kind == EXPR_INCDEC && !e->incdec_is_prefix && e->lhs && e->lhs->kind == EXPR_VAR && e->op_col0 >= 0) {
        int n = (int)strlen(e->lhs->text ? e->lhs->text : "");
        int c = e->op_col0 - n;
        return (c < 0) ? 0 : c;
    }
    if (e->op_col0 >= 0) return e->op_col0;
    return 0;
}

int expr_min_line0(const Expr *e) {
    if (!e) return -1;
    int best = -1;

    if (e->op_line0 >= 0) best = e->op_line0;
    if (e->lparen_line0 >= 0) best = (best < 0 || e->lparen_line0 < best) ? e->lparen_line0 : best;
    if (e->close_line0 >= 0) best = (best < 0 || e->close_line0 < best) ? e->close_line0 : best;

    int v;
    v = expr_min_line0(e->lhs);
    if (v >= 0) best = (best < 0 || v < best) ? v : best;
    v = expr_min_line0(e->mid);
    if (v >= 0) best = (best < 0 || v < best) ? v : best;
    v = expr_min_line0(e->rhs);
    if (v >= 0) best = (best < 0 || v < best) ? v : best;
    v = expr_min_line0(e->index_base);
    if (v >= 0) best = (best < 0 || v < best) ? v : best;

    for (int i = 0; i < e->item_count; i++) {
        v = expr_min_line0(e->items ? e->items[i] : NULL);
        if (v >= 0) best = (best < 0 || v < best) ? v : best;
    }
    for (int i = 0; i < e->call_arg_count; i++) {
        v = expr_min_line0(e->call_args ? e->call_args[i] : NULL);
        if (v >= 0) best = (best < 0 || v < best) ? v : best;
    }
    for (int i = 0; i < e->index_count; i++) {
        v = expr_min_line0(e->index_items ? e->index_items[i] : NULL);
        if (v >= 0) best = (best < 0 || v < best) ? v : best;
    }
    return best;
}

void expr_min_col0_on_line_rec(const Expr *e, int line0, int *best_col0) {
    if (!e || !best_col0) return;
    /*
     * For "leading token" inference (e.g., if keyword col), most binary operator anchors ('+', '==', etc.)
     * are not useful because they are not leading tokens. Unary/prefix operators *are* leading tokens.
     */
    bool consider_op_anchor = false;
    if (e->kind == EXPR_UNOP) {
        consider_op_anchor = true;
    } else if (e->kind == EXPR_INCDEC && e->incdec_is_prefix) {
        consider_op_anchor = true;
    } else if (e->kind != EXPR_BINOP && e->kind != EXPR_INCDEC && e->kind != EXPR_TERNARY) {
        consider_op_anchor = true;
    }
    if (consider_op_anchor && e->op_line0 == line0 && e->op_col0 >= 0) {
        if (*best_col0 < 0 || e->op_col0 < *best_col0) *best_col0 = e->op_col0;
    }
    if (e->lparen_line0 == line0 && e->lparen_col0 >= 0) {
        if (*best_col0 < 0 || e->lparen_col0 < *best_col0) *best_col0 = e->lparen_col0;
    }
    if (e->close_line0 == line0 && e->close_col0 >= 0) {
        if (*best_col0 < 0 || e->close_col0 < *best_col0) *best_col0 = e->close_col0;
    }

    expr_min_col0_on_line_rec(e->lhs, line0, best_col0);
    expr_min_col0_on_line_rec(e->mid, line0, best_col0);
    expr_min_col0_on_line_rec(e->rhs, line0, best_col0);
    expr_min_col0_on_line_rec(e->index_base, line0, best_col0);

    if (e->items) {
        for (int i = 0; i < e->item_count; i++) expr_min_col0_on_line_rec(e->items[i], line0, best_col0);
    }
    if (e->call_args) {
        for (int i = 0; i < e->call_arg_count; i++) expr_min_col0_on_line_rec(e->call_args[i], line0, best_col0);
    }
    if (e->index_items) {
        for (int i = 0; i < e->index_count; i++) expr_min_col0_on_line_rec(e->index_items[i], line0, best_col0);
    }
}

int expr_min_col0_on_line(const Expr *e, int line0) {
    int best = -1;
    expr_min_col0_on_line_rec(e, line0, &best);
    return best;
}

int if_keyword_col0_from_cond(const Expr *cond, int if_line0, bool in_function) {
    int cond_min_col0 = expr_min_col0_on_line(cond, if_line0);
    int fallback = in_function ? 4 : 0;

    /* Two common baseline styles:
       - "if ("  -> condition starts at (if_col0 + 4)
       - "if("   -> condition starts at (if_col0 + 3) */
    int cand_space = (cond_min_col0 >= 4) ? (cond_min_col0 - 4) : -1;
    int cand_nospace = (cond_min_col0 >= 3) ? (cond_min_col0 - 3) : -1;

    int best = -1;
    int best_dist = 0x7fffffff;
    if (cand_space >= 0) {
        int d = cand_space - fallback;
        if (d < 0) d = -d;
        best = cand_space;
        best_dist = d;
    }
    if (cand_nospace >= 0) {
        int d = cand_nospace - fallback;
        if (d < 0) d = -d;
        if (d < best_dist) {
            best = cand_nospace;
            best_dist = d;
        }
    }
    if (best >= 0) return best;
    return fallback;
}

int if_lparen_col0_from_cond(const Expr *cond, int if_line0, int if_col0) {
    int cond_min_col0 = expr_min_col0_on_line(cond, if_line0);
    if (cond_min_col0 >= 0) {
        int lp = cond_min_col0 - 1;
        int min_lp = if_col0 + 2; /* after "if" */
        if (lp < min_lp) lp = min_lp;
        return lp;
    }
    return if_col0 + 3; /* default "if (" */
}

bool emit_if_keyword_and_lparen(AelEmitter *out, int line0, int if_col0, int lparen_col0) {
    if (!out) return false;
    if (!ael_emit_at(out, line0, if_col0)) return false;
    if (!ael_emit_text(out, "if")) return false;
    if (!ael_emit_at(out, line0, lparen_col0)) return false;
    return ael_emit_char(out, '(');
}

Expr *unwrap_logical_not(Expr *e) {
    if (!e) return NULL;
    if (e->kind == EXPR_UNOP && e->op_code == 3 && e->rhs) {
        Expr *inner = e->rhs;
        e->rhs = NULL;
        expr_free(e);
        return inner;
    }
    return e;
}

bool ael_emit_at_expr_soft(AelEmitter *out, int line0, int col0) {
    if (!out) return false;
    if (!out->strict_pos) return true;
    if (ael_emit_at(out, line0, col0)) return true;
    if (!out->strict_pos) return true;
    if (out->last_fail_reason == AEL_EMIT_FAIL_BACKWARD_COL && line0 == out->line0) {
        int back = out->col0 - col0;
        if (back >= 0 && back <= 8) {
            out->last_fail_reason = AEL_EMIT_FAIL_NONE;
            return true;
        }
    }
    return false;
}

bool expr_starts_with_unop_code(const Expr *e, int op_code) {
    return e && e->kind == EXPR_UNOP && e->op_code == op_code;
}

bool ir_inst_is_load_trueish(const IRInst *inst) {
    if (!inst) return false;
    if (inst->op == OP_LOAD_TRUE) return true;
    if (inst->op == OP_LOAD_INT && inst->has_arg1 && inst->arg1 == 1) return true;
    if (inst->op == 5 && inst->has_arg1 && inst->arg1 == 1) return true; /* LOAD_BOOL */
    return false;
}

bool emit_expr(AelEmitter *out, const Expr *e, int parent_prec) {
    if (!e) return false;

    if (e->kind == EXPR_INT) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", e->int_value);
        return ael_emit_text(out, buf);
    }
    if (e->kind == EXPR_BOOL) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        return ael_emit_text(out, e->bool_value ? "TRUE" : "FALSE");
    }
    if (e->kind == EXPR_REAL) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        char buf[128];
        format_real_token(e->num_value, buf, sizeof(buf));
        return ael_emit_text(out, buf);
    }
    if (e->kind == EXPR_IMAG) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        char buf[160];
        format_imag_token(e->num_value, buf, sizeof(buf));
        if (!ael_emit_text(out, buf)) return false;
        return ael_emit_char(out, 'i');
    }
    if (e->kind == EXPR_NULL) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        /* Official AEL uses uppercase NULL. */
        return ael_emit_text(out, "NULL");
    }
    if (e->kind == EXPR_VAR) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        if ((e->flags & EXPR_FLAG_ADDR_OF) != 0) {
            if (!ael_emit_char(out, '&')) return false;
        }
        return ael_emit_text(out, e->text ? e->text : "");
    }
    if (e->kind == EXPR_STR) {
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        return emit_escaped_string(out, e->text);
    }
    if (e->kind == EXPR_LIST) {
        if (!ael_emit_char(out, '{')) return false;
        for (int i = 0; i < e->item_count; i++) {
            if (i != 0) {
                if (!ael_emit_char(out, ',')) return false;
            }
            if (!emit_expr(out, e->items[i], 0)) return false;
        }
        if (e->close_line0 >= 0 && e->close_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->close_line0, e->close_col0)) return false;
        }
        return ael_emit_char(out, '}');
    }

    if (e->kind == EXPR_CALL) {
        /* callee */
        if (e->lhs && e->lhs->kind == EXPR_VAR && e->lparen_line0 >= 0 && e->lparen_col0 >= 0) {
            int n = (int)strlen(e->lhs->text ? e->lhs->text : "");
            int name_col0 = e->lparen_col0 - n;
            if (name_col0 < 0) name_col0 = 0;
            if (!ael_emit_at_expr_soft(out, e->lparen_line0, name_col0)) return false;
            if (!ael_emit_text(out, e->lhs->text ? e->lhs->text : "")) return false;
        } else {
            if (!emit_expr(out, e->lhs, 0)) return false;
        }
        if (e->lparen_line0 >= 0 && e->lparen_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->lparen_line0, e->lparen_col0)) return false;
        }
        if (!ael_emit_char(out, '(')) return false;
        for (int i = 0; i < e->call_arg_count; i++) {
            if (i != 0) {
                if (!ael_emit_char(out, ',')) return false;
            }
            if (!emit_expr(out, e->call_args[i], 0)) return false;
        }
        return ael_emit_char(out, ')');
    }

    if (e->kind == EXPR_INDEX) {
        if (!emit_expr(out, e->index_base, 0)) return false;
        if (!ael_emit_char(out, '[')) return false;
        for (int i = 0; i < e->index_count; i++) {
            if (i != 0) {
                if (!ael_emit_char(out, ',')) return false;
            }
            if (!emit_expr(out, e->index_items[i], 0)) return false;
        }
        if (!ael_emit_char(out, ']')) return false;
        return true;
    }

    if (e->kind == EXPR_CALLARGS) {
        return false;
    }

    if (e->kind == EXPR_INCDEC) {
        const char *op = e->incdec_is_inc ? "++" : "--";
        if (e->incdec_is_prefix) {
            if (e->op_line0 >= 0 && e->op_col0 >= 0) {
                if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
            }
            if (!ael_emit_text(out, op)) return false;
            return emit_expr(out, e->lhs, 0);
        }

        /* postfix */
        if (e->lhs && e->lhs->kind == EXPR_VAR && e->op_line0 >= 0 && e->op_col0 >= 0) {
            int n = (int)strlen(e->lhs->text ? e->lhs->text : "");
            int var_col0 = e->op_col0 - n;
            if (var_col0 < 0) var_col0 = 0;
            if (!ael_emit_at_expr_soft(out, e->op_line0, var_col0)) return false;
            if (!ael_emit_text(out, e->lhs->text ? e->lhs->text : "")) return false;
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
            return ael_emit_text(out, op);
        }

        if (!emit_expr(out, e->lhs, 0)) return false;
        return ael_emit_text(out, op);
    }

    if (e->kind == EXPR_TERNARY) {
        /* Ternary has very low precedence; parenthesize when nested (e.g. x + (c ? a : b)). */
        bool wrap = (parent_prec > 0);
        if (wrap) {
            if (!ael_emit_char(out, '(')) return false;
        }
        bool has_group_parens = (e->lparen_line0 >= 0 && e->lparen_col0 >= 0 &&
                                 e->op_line0 >= 0 && e->op_col0 >= 0 &&
                                 e->lparen_line0 == e->op_line0);
        int rparen_col0 = -1;
        if (has_group_parens) {
            /* Baseline ternary typically groups the condition: "(cond) ? a : b". */
            rparen_col0 = e->op_col0 - 2; /* ") " before '?' */
            if (rparen_col0 < e->lparen_col0 + 1) rparen_col0 = e->lparen_col0 + 1;
            if (!ael_emit_at_expr_soft(out, e->lparen_line0, e->lparen_col0)) return false;
            if (!ael_emit_char(out, '(')) return false;
        }

        if (!emit_expr(out, e->lhs, 0)) return false;

        if (has_group_parens && rparen_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, rparen_col0)) return false;
            if (!ael_emit_char(out, ')')) return false;
        }
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        } else {
            if (!ael_emit_char(out, ' ')) return false;
        }
        if (!ael_emit_char(out, '?')) return false;
        if (e->mid && (e->mid->op_line0 < 0 || e->mid->op_col0 < 0) &&
            (e->mid->kind == EXPR_INT || e->mid->kind == EXPR_REAL || e->mid->kind == EXPR_IMAG ||
             e->mid->kind == EXPR_NULL || e->mid->kind == EXPR_STR)) {
            if (!ael_emit_char(out, ' ')) return false;
        }
        if (!emit_expr(out, e->mid, 0)) return false;
        if (e->close_line0 >= 0 && e->close_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->close_line0, e->close_col0)) return false;
        } else {
            if (!ael_emit_char(out, ' ')) return false;
        }
        if (!ael_emit_char(out, ':')) return false;
        if (e->rhs && (e->rhs->op_line0 < 0 || e->rhs->op_col0 < 0) &&
            (e->rhs->kind == EXPR_INT || e->rhs->kind == EXPR_REAL || e->rhs->kind == EXPR_IMAG ||
             e->rhs->kind == EXPR_NULL || e->rhs->kind == EXPR_STR)) {
            /* Avoid forcing "x* 1e-6" when the baseline uses "x*1e-6" (common in unit conversions). */
            if (e->op_code != 12) {
                if (!ael_emit_char(out, ' ')) return false;
            }
        }
        if (!emit_expr(out, e->rhs, 0)) return false;
        if (wrap) {
            if (!ael_emit_char(out, ')')) return false;
        }
        return true;
    }

    const char *op = op_code_to_str(e->op_code);
    int prec = op_precedence(e->op_code);
    bool need_paren = prec != 0 && prec < parent_prec;
    if (e->kind == EXPR_BINOP && (e->op_code == 16 || e->op_code == 47)) {
        /* Assignment/comma have very low precedence; parenthesize when nested to keep semantics. */
        prec = 0;
        need_paren = (parent_prec > 0);
    }

    if (need_paren) {
        if (!ael_emit_char(out, '(')) return false;
    }

    if (e->kind == EXPR_UNOP) {
        if (!op) return false;
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        }
        if (!ael_emit_text(out, op)) return false;
        if (!out->strict_pos && e->op_code == 15 /* unary '-' */ && expr_starts_with_unop_code(e->rhs, 15)) {
            if (!ael_emit_char(out, ' ')) return false;
        }
        if (!emit_expr(out, e->rhs, prec)) return false;
    } else if (e->kind == EXPR_BINOP) {
        /* Best-effort unit-suffix recovery: only safe for numeric literals (e.g. "5um"), never for variables.
           (Emitting "varum" changes meaning and breaks roundtrips like "W = default_W*1e-6;".) */
        if (e->op_code == 12 && e->lhs && e->rhs && e->rhs->kind == EXPR_REAL &&
            (e->lhs->kind == EXPR_INT || e->lhs->kind == EXPR_REAL)) {
            const char *unit = unit_suffix_from_multiplier(e->rhs->num_value);
            if (unit) {
                if (!emit_expr(out, e->lhs, prec)) return false;
                if (e->op_line0 >= 0 && e->op_col0 >= 0) {
                    if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
                } else {
                    if (!ael_emit_char(out, ' ')) return false;
                }
                if (!ael_emit_text(out, unit)) return false;
                if (need_paren) {
                    if (!ael_emit_char(out, ')')) return false;
                }
                return true;
            }
        }
        int lhs_parent_prec = prec;
        if (binop_lhs_force_paren(e->op_code, e->lhs)) lhs_parent_prec = prec + 1;
        if (!emit_expr(out, e->lhs, lhs_parent_prec)) return false;
        if (!op) return false;
        if (e->op_line0 >= 0 && e->op_col0 >= 0) {
            if (!ael_emit_at_expr_soft(out, e->op_line0, e->op_col0)) return false;
        } else {
            if (!ael_emit_char(out, ' ')) return false;
        }
        if (!ael_emit_text(out, op)) return false;
        if (!out->strict_pos && e->rhs) {
            /* Avoid forming '++'/'--' tokens when emitting binary +/- followed by unary +/- in loose mode. */
            if ((e->op_code == 10 /* '+' */ && expr_starts_with_unop_code(e->rhs, 10)) ||
                (e->op_code == 11 /* '-' */ && expr_starts_with_unop_code(e->rhs, 15))) {
                if (!ael_emit_char(out, ' ')) return false;
            }
        }
        /* Heuristic: some IR logs attach the RHS column of scientific literals like "1e-6"
           to the 'e' character, which would make StrictPos emission insert a space: "x* 1e-6".
           When we can see this exact off-by-one for multiply, emit the literal without honoring
           its own position so it starts immediately after '*'. */
        if (e->op_code == 12 && e->rhs && e->rhs->kind == EXPR_REAL &&
            e->op_line0 >= 0 && e->op_col0 >= 0 &&
            e->rhs->op_line0 == e->op_line0 && e->rhs->op_col0 == e->op_col0 + 2) {
            char buf[128];
            format_real_token(e->rhs->num_value, buf, sizeof(buf));
            if (!ael_emit_text(out, buf)) return false;
            if (need_paren) {
                if (!ael_emit_char(out, ')')) return false;
            }
            return true;
        }
        if (e->rhs && (e->rhs->op_line0 < 0 || e->rhs->op_col0 < 0) &&
            (e->rhs->kind == EXPR_INT || e->rhs->kind == EXPR_REAL || e->rhs->kind == EXPR_IMAG ||
             e->rhs->kind == EXPR_NULL || e->rhs->kind == EXPR_STR)) {
            /* Avoid forcing "x* 1e-6" when the baseline uses "x*1e-6" (common in unit conversions). */
            if (e->op_code != 12) {
                if (!ael_emit_char(out, ' ')) return false;
            }
        }
        int rhs_parent_prec = prec;
        if (binop_rhs_force_paren(e->op_code, e->rhs)) rhs_parent_prec = prec + 1;
        if (!emit_expr(out, e->rhs, rhs_parent_prec)) return false;
    } else {
        return false;
    }

    if (need_paren) {
        if (!ael_emit_char(out, ')')) return false;
    }
    return true;
}

bool emit_expr_addr(AelEmitter *out, Expr *e, int parent_prec) {
    if (!e) return false;
    expr_mark_addr_of(e, true);
    return emit_expr(out, e, parent_prec);
}

bool stack_push(Expr ***stk, size_t *len, size_t *cap, Expr *e) {
    if (*len + 1 > *cap) {
        size_t nc = (*cap == 0) ? 64 : (*cap * 2);
        Expr **ns = (Expr **)realloc(*stk, nc * sizeof(Expr *));
        if (!ns) return false;
        *stk = ns;
        *cap = nc;
    }
    (*stk)[(*len)++] = e;
    return true;
}

Expr *stack_pop(Expr **stk, size_t *len) {
    if (*len == 0) return NULL;
    return stk[--(*len)];
}

void stack_clear(Expr **stk, size_t *len) {
    for (size_t i = 0; i < *len; i++) expr_free(stk[i]);
    *len = 0;
}

Expr *stack_pop_stmt_expr(Expr **stk, size_t *len) {
    while (*len > 0) {
        Expr *e = stack_pop(stk, len);
        if (!e) continue;
        if (e->kind == EXPR_CALLARGS) {
            expr_free(e);
            continue;
        }
        return e;
    }
    return NULL;
}

bool parse_expr_range(const IRProgram *program, size_t start, size_t end, Expr **out_expr, char *err, size_t err_cap) {
    if (out_expr) *out_expr = NULL;
    if (!program || !out_expr) return false;
    if (start > end || end > program->count) return false;

    Expr **stk = NULL;
    size_t len = 0, cap = 0;
    size_t bad_i = (size_t)-1;
    int bad_op_code = -1;
    const char *bad_reason = NULL;

    for (size_t i = start; i < end; i++) {
        const IRInst *inst = &program->insts[i];
        if (ir_inst_is_scope_bookkeeping(inst)) {
            continue;
        }
        if (inst->op == OP_LOAD_INT && inst->has_arg1) {
            Expr *e = expr_new(EXPR_INT);
            if (!e) goto oom;
            e->int_value = inst->arg1;
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == 5 && inst->has_arg1) { /* LOAD_BOOL */
            Expr *e = expr_new(EXPR_BOOL);
            if (!e) goto oom;
            e->bool_value = (inst->arg1 != 0);
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_LOAD_TRUE) {
            Expr *e = expr_new(EXPR_BOOL);
            if (!e) goto oom;
            e->bool_value = (inst->has_arg1 ? (inst->arg1 != 0) : 1);
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_LOAD_REAL && inst->has_num_val) {
            Expr *e = expr_new(EXPR_REAL);
            if (!e) goto oom;
            e->num_value = inst->num_val;
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_LOAD_IMAG && inst->has_num_val) {
            Expr *e = expr_new(EXPR_IMAG);
            if (!e) goto oom;
            e->num_value = inst->num_val;
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_LOAD_NULL) {
            Expr *e = expr_new(EXPR_NULL);
            if (!e) goto oom;
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_LOAD_STR) {
            Expr *e = expr_new(EXPR_STR);
            if (!e) goto oom;
            e->text = inst->str ? _strdup(inst->str) : _strdup("");
            if (!e->text) goto oom;
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_LOAD_VAR) {
            Expr *e = expr_new(EXPR_VAR);
            if (!e) goto oom;
            e->text = inst->str ? _strdup(inst->str) : _strdup("");
            if (!e->text) goto oom;
            if (!stack_push(&stk, &len, &cap, e)) goto oom;
            continue;
        }
        if (inst->op == OP_OP) {
            int op_code = inst->has_arg1 ? inst->arg1 : 0;

            /* Template markers that should not be interpreted as operators here.
               Note: OP=59 is handled by the nested ternary matcher below (then treated as bookkeeping). */
            if (op_code == 60 || op_code == 61 || op_code == 65) {
                continue;
            }

            /* Some short-circuit encodings use a duplicated leading marker (e.g. OP=62 OP=62 ...).
               Skip the first marker and let the second one drive the template recognition. */
            if ((op_code == 62 || op_code == 63) && i + 1 < end &&
                program->insts[i + 1].op == OP_OP && program->insts[i + 1].has_arg1 &&
                program->insts[i + 1].arg1 == op_code) {
                continue;
            }

            /* Some templates include a trailing short-circuit marker directly followed by SET_LABEL.
               When parsing a sub-range (e.g. RHS-only), treat this marker as non-expression bookkeeping. */
            if ((op_code == 62 || op_code == 63) && i + 1 < end &&
                program->insts[i + 1].op == OP_SET_LABEL && program->insts[i + 1].has_arg1) {
                continue;
            }

            /* Nested ternary template inside ranges. */
            if (op_code == 59 && i + 5 < end &&
                program->insts[i + 1].op == OP_ADD_LABEL &&
                program->insts[i + 2].op == OP_ADD_LABEL &&
                program->insts[i + 3].op == OP_OP && program->insts[i + 3].has_arg1 && program->insts[i + 3].arg1 == 3 &&
                program->insts[i + 4].op == OP_BRANCH_TRUE && program->insts[i + 4].has_arg1 &&
                program->insts[i + 5].op == OP_OP && program->insts[i + 5].has_arg1 && program->insts[i + 5].arg1 == 61) {

                int false_label = program->insts[i + 4].arg1;

                size_t idx_op60 = (size_t)-1;
                for (size_t j = i + 6; j + 3 < end; j++) {
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
                    for (size_t j = else_start; j + 1 < end; j++) {
                        if (program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == 65 &&
                            program->insts[j + 1].op == OP_SET_LABEL && program->insts[j + 1].has_arg1 &&
                            program->insts[j + 1].arg1 == end_label) {
                            idx_op65 = j;
                            break;
                        }
                    }
                    if (idx_op65 != (size_t)-1) {

                        Expr *cond = stack_pop(stk, &len);
                        if (!cond) goto bad;
                        Expr *then_e = NULL;
                        Expr *else_e = NULL;
                        if (!parse_expr_range(program, i + 6, idx_op60, &then_e, err, err_cap)) {
                            expr_free(cond);
                            goto bad;
                        }
                        if (!parse_expr_range(program, else_start, idx_op65, &else_e, err, err_cap)) {
                            expr_free(cond);
                            expr_free(then_e);
                            goto bad;
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
                        if (!stack_push(&stk, &len, &cap, te)) {
                            expr_free(te);
                            goto oom;
                        }
                        i = idx_op65 + 1;
                        continue;
                    }
                }
            }

            /* Nested short-circuit OR/AND templates inside ranges.
             * Some encodings use: ADD_LABEL; OP=62/63; ... (outer form)
             * Others use: OP=62/63; OP=62/63; ...      (nested-in-RHS form)
             */
            if ((op_code == 62 || op_code == 63) && i >= 1 &&
                (program->insts[i - 1].op == OP_ADD_LABEL ||
                 (program->insts[i - 1].op == OP_OP && program->insts[i - 1].has_arg1 &&
                  program->insts[i - 1].arg1 == op_code))) {
                size_t idx_bt = (size_t)-1;
                int end_label = -1;
                if (op_code == 63) {
                    if (i + 2 < end &&
                        program->insts[i + 1].op == OP_OP && program->insts[i + 1].has_arg1 && program->insts[i + 1].arg1 == 36 &&
                        program->insts[i + 2].op == OP_BRANCH_TRUE && program->insts[i + 2].has_arg1) {
                        idx_bt = i + 2;
                        end_label = program->insts[i + 2].arg1;
                    }
                } else {
                    if (i + 3 < end &&
                        program->insts[i + 1].op == OP_OP && program->insts[i + 1].has_arg1 && program->insts[i + 1].arg1 == 36 &&
                        program->insts[i + 2].op == OP_OP && program->insts[i + 2].has_arg1 && program->insts[i + 2].arg1 == 3 &&
                        program->insts[i + 3].op == OP_BRANCH_TRUE && program->insts[i + 3].has_arg1) {
                        idx_bt = i + 3;
                        end_label = program->insts[i + 3].arg1;
                    }
                }
                if (idx_bt != (size_t)-1 && idx_bt + 2 < end &&
                    program->insts[idx_bt + 1].op == OP_OP && program->insts[idx_bt + 1].has_arg1 && program->insts[idx_bt + 1].arg1 == 0 &&
                    program->insts[idx_bt + 2].op == OP_OP && program->insts[idx_bt + 2].has_arg1 && program->insts[idx_bt + 2].arg1 == op_code) {

                    size_t rhs_start = idx_bt + 3;
                    size_t rhs_marker = (size_t)-1;
                    for (size_t j = rhs_start; j + 1 < end; j++) {
                        if (program->insts[j].op == OP_OP && program->insts[j].has_arg1 && program->insts[j].arg1 == op_code &&
                            program->insts[j + 1].op == OP_SET_LABEL && program->insts[j + 1].has_arg1 && program->insts[j + 1].arg1 == end_label) {
                            rhs_marker = j;
                            break;
                        }
                    }
                    if (rhs_marker != (size_t)-1) {
                        Expr *lhs = stack_pop(stk, &len);
                        if (!lhs) goto bad;
                        Expr *rhs = NULL;
                        if (!parse_expr_range(program, rhs_start, rhs_marker, &rhs, err, err_cap)) {
                            /* Fallback: include the trailing marker + SET_LABEL so deeper nested templates
                               that share the same end_label can be recognized. */
                            size_t rhs_end = rhs_marker + 2;
                            if (rhs_end > program->count) rhs_end = program->count;
                            if (!parse_expr_range(program, rhs_start, rhs_end, &rhs, err, err_cap)) {
                                expr_free(lhs);
                                goto bad;
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
                        if (!stack_push(&stk, &len, &cap, e)) {
                            expr_free(e);
                            goto oom;
                        }
                        i = rhs_marker + 1;
                        continue;
                    }
                }
            }
            if (op_code == 17) {
                if (len > 0) {
                    Expr *top = stk[len - 1];
                    if (top && top->kind != EXPR_CALLARGS) {
                        top->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
                        top->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
                    }
                }
                continue;
            }
            if (op_code == 0 || op_code == 53) continue;
            if (op_code == 46) {
                int n = inst->has_a4 ? inst->a4 : 0;
                if (n < 0) n = 0;
                Expr **items = NULL;
                if (n > 0) {
                    items = (Expr **)calloc((size_t)n, sizeof(Expr *));
                    if (!items) goto oom;
                    for (int k = n - 1; k >= 0; k--) {
                        items[k] = stack_pop(stk, &len);
                        if (!items[k]) {
                            for (int t = k; t < n; t++) expr_free(items[t]);
                            free(items);
                            goto bad;
                        }
                    }
                }
                Expr *e = expr_new(EXPR_LIST);
                if (!e) {
                    for (int k = 0; k < n; k++) expr_free(items[k]);
                    free(items);
                    goto oom;
                }
                e->items = items;
                e->item_count = n;
                if (!stack_push(&stk, &len, &cap, e)) {
                    expr_free(e);
                    goto oom;
                }
                continue;
            }
            if (op_code == 56) {
                Expr *m = expr_new(EXPR_CALLARGS);
                if (!m) goto oom;
                m->call_argc = inst->has_a4 ? inst->a4 : 0;
                if (m->call_argc < 0) m->call_argc = 0;
                m->lparen_line0 = inst->has_arg2 ? inst->arg2 : -1;
                m->lparen_col0 = inst->has_arg3 ? inst->arg3 : -1;
                if (!stack_push(&stk, &len, &cap, m)) {
                    expr_free(m);
                    goto oom;
                }
                continue;
            }
            if (op_code == 48) {
                if (len > 0 && stk[len - 1] && stk[len - 1]->kind == EXPR_CALLARGS) {
                    Expr *m = stack_pop(stk, &len);
                    int argc = m ? m->call_argc : 0;
                    if (argc < 0) argc = 0;

                    Expr **args = NULL;
                    if (argc > 0) {
                        args = (Expr **)calloc((size_t)argc, sizeof(Expr *));
                        if (!args) {
                            expr_free(m);
                            goto oom;
                        }
                        for (int k = argc - 1; k >= 0; k--) {
                            args[k] = stack_pop(stk, &len);
                            if (!args[k]) {
                                for (int t = k; t < argc; t++) expr_free(args[t]);
                                free(args);
                                expr_free(m);
                                goto bad;
                            }
                        }
                    }
                    Expr *callee = stack_pop(stk, &len);
                    if (!callee) {
                        for (int t = 0; t < argc; t++) expr_free(args ? args[t] : NULL);
                        free(args);
                        expr_free(m);
                        goto bad;
                    }
                    Expr *ce = expr_new(EXPR_CALL);
                    if (!ce) {
                        expr_free(callee);
                        for (int t = 0; t < argc; t++) expr_free(args ? args[t] : NULL);
                        free(args);
                        expr_free(m);
                        goto oom;
                    }
                    ce->lhs = callee;
                    ce->call_args = args;
                    ce->call_arg_count = argc;
                    ce->lparen_line0 = m ? m->lparen_line0 : -1;
                    ce->lparen_col0 = m ? m->lparen_col0 : -1;
                    ce->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
                    ce->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
                    expr_free(m);
                    if (!stack_push(&stk, &len, &cap, ce)) {
                        expr_free(ce);
                        goto oom;
                    }
                    continue;
                }

                int group_counts[32];
                int group_line0[32];
                int group_col0[32];
                int group_n = 0;
                int total_index_items = 0;
                size_t run_end = i;
                for (size_t j = i; j < end && group_n < (int)(sizeof(group_counts) / sizeof(group_counts[0])); j++) {
                    const IRInst *mj = &program->insts[j];
                    if (mj->op != OP_OP || !mj->has_arg1 || mj->arg1 != 48) break;
                    int a4j = mj->has_a4 ? mj->a4 : 0;
                    int ic = a4j - 1;
                    if (ic <= 0) break;
                    group_counts[group_n] = ic;
                    group_line0[group_n] = mj->has_arg2 ? mj->arg2 : -1;
                    group_col0[group_n] = mj->has_arg3 ? mj->arg3 : -1;
                    group_n++;
                    total_index_items += ic;
                    run_end = j + 1;
                }
                if (group_n <= 0 || total_index_items <= 0) goto bad;

                Expr **all_idxs = (Expr **)calloc((size_t)total_index_items, sizeof(Expr *));
                if (!all_idxs) goto oom;
                for (int k = total_index_items - 1; k >= 0; k--) {
                    all_idxs[k] = stack_pop(stk, &len);
                    if (!all_idxs[k]) {
                        for (int t = k; t < total_index_items; t++) expr_free(all_idxs[t]);
                        free(all_idxs);
                        goto bad;
                    }
                }
                Expr *base = stack_pop(stk, &len);
                if (!base) {
                    for (int t = 0; t < total_index_items; t++) expr_free(all_idxs[t]);
                    free(all_idxs);
                    goto bad;
                }
                Expr *cur = base;
                int off = 0;
                for (int g = 0; g < group_n; g++) {
                    int ic = group_counts[g];
                    Expr **idxs = (Expr **)calloc((size_t)ic, sizeof(Expr *));
                    if (!idxs) {
                        expr_free(cur);
                        for (int t = off; t < total_index_items; t++) expr_free(all_idxs[t]);
                        free(all_idxs);
                        goto oom;
                    }
                    for (int t = 0; t < ic; t++) {
                        idxs[t] = all_idxs[off + t];
                        all_idxs[off + t] = NULL;
                    }
                    Expr *ie = expr_new(EXPR_INDEX);
                    if (!ie) {
                        expr_free(cur);
                        for (int t = 0; t < ic; t++) expr_free(idxs[t]);
                        free(idxs);
                        for (int t = off + ic; t < total_index_items; t++) expr_free(all_idxs[t]);
                        free(all_idxs);
                        goto oom;
                    }
                    ie->index_base = cur;
                    ie->index_items = idxs;
                    ie->index_count = ic;
                    ie->op_line0 = group_line0[g];
                    ie->op_col0 = group_col0[g];
                    cur = ie;
                    off += ic;
                }
                free(all_idxs);
                if (!stack_push(&stk, &len, &cap, cur)) {
                    expr_free(cur);
                    goto oom;
                }
                i = run_end - 1;
                continue;
            }
            if (op_code == 36) {
                if (len == 0 || !stk[len - 1]) {
                    bad_i = i;
                    bad_op_code = op_code;
                    bad_reason = "stack underflow (dup)";
                    goto bad;
                }
                Expr *dup = expr_clone(stk[len - 1]);
                if (!dup) goto oom;
                dup->flags |= EXPR_FLAG_LVALUE_DUP;
                dup->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
                dup->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
                if (!stack_push(&stk, &len, &cap, dup)) {
                    expr_free(dup);
                    goto oom;
                }
                continue;
            }
            if (op_code == 31 || op_code == 32 || op_code == 33 || op_code == 34) {
                Expr *v = stack_pop(stk, &len);
                if (!v) {
                    bad_i = i;
                    bad_op_code = op_code;
                    bad_reason = "stack underflow (inc/dec)";
                    goto bad;
                }
                Expr *e = expr_new(EXPR_INCDEC);
                if (!e) {
                    expr_free(v);
                    goto oom;
                }
                e->lhs = v;
                e->incdec_is_inc = (op_code == 31 || op_code == 33);
                e->incdec_is_prefix = (op_code == 31 || op_code == 32);
                e->op_code = op_code;
                e->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
                e->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
                if (!stack_push(&stk, &len, &cap, e)) {
                    expr_free(e);
                    goto oom;
                }
                continue;
            }
            const char *op_str = op_code_to_str(op_code);
            if (!op_str) {
                bad_i = i;
                bad_op_code = op_code;
                bad_reason = "unknown op_code";
                goto bad;
            }
            if (op_code == 3 || op_code == 15) {
                Expr *a = stack_pop(stk, &len);
                if (!a) {
                    bad_i = i;
                    bad_op_code = op_code;
                    bad_reason = "stack underflow (unary)";
                    goto bad;
                }
                Expr *e = expr_new(EXPR_UNOP);
                if (!e) {
                    expr_free(a);
                    goto oom;
                }
                e->op_code = op_code;
                e->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
                e->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
                e->rhs = a;
                if (!stack_push(&stk, &len, &cap, e)) {
                    expr_free(e);
                    goto oom;
                }
                continue;
            }
            Expr *rhs = stack_pop(stk, &len);
            Expr *lhs = stack_pop(stk, &len);
            if (!rhs || !lhs) {
                expr_free(rhs);
                expr_free(lhs);
                bad_i = i;
                bad_op_code = op_code;
                bad_reason = "stack underflow (binary)";
                goto bad;
            }
            Expr *e = expr_new(EXPR_BINOP);
            if (!e) {
                expr_free(rhs);
                expr_free(lhs);
                goto oom;
            }
            e->op_code = op_code;
            e->op_line0 = inst->has_arg2 ? inst->arg2 : -1;
            e->op_col0 = inst->has_arg3 ? inst->arg3 : -1;
            e->lhs = lhs;
            e->rhs = rhs;
            if (!stack_push(&stk, &len, &cap, e)) {
                expr_free(e);
                goto oom;
            }
            continue;
        }
        /* ignore other opcodes in range */
    }

    if (len != 1) {
        bad_i = end ? (end - 1) : start;
        bad_reason = "final stack depth != 1";
        goto bad;
    }
    *out_expr = stk[0];
    free(stk);
    return true;

bad:
    if (err && err_cap) {
        if (bad_reason) {
            snprintf(err, err_cap, "cannot parse expr range %zu..%zu (at=%zu op=%d len=%zu: %s)", start, end, bad_i, bad_op_code, len, bad_reason);
        } else {
            snprintf(err, err_cap, "cannot parse expr range %zu..%zu (len=%zu)", start, end, len);
        }
    }
    stack_clear(stk, &len);
    free(stk);
    return false;

oom:
    if (err && err_cap) snprintf(err, err_cap, "out of memory");
    stack_clear(stk, &len);
    free(stk);
    return false;
}





bool if_close_braced_else_on_depth_exit(AelEmitter *out, IfCtx *if_stack, int *if_sp, int cur_depth, int *anon_sp, int *anon_stack) {
    if (!out || !if_stack || !if_sp) return false;
    while (*if_sp > 0) {
        IfCtx *ctx = &if_stack[*if_sp - 1];
        if (!(ctx->stage == 2 && ctx->else_brace_style)) break;
        /* When we are at depth <= if-header depth, we have exited the braced else body (depth=ctx->depth+1). */
        if (cur_depth > ctx->depth) break;
        int close_col0 = ctx->depth * 4;
        if (out->col0 != 0) {
            if (!ael_emit_char(out, '\n')) return false;
        }
        if (!ael_emit_at(out, out->line0, close_col0)) return false;
        if (!ael_emit_text(out, "}\n")) return false;
        if (anon_sp && anon_stack) {
            anon_depth_pop_expected(anon_sp, anon_stack, ctx->depth + 1, true);
        }
        (*if_sp)--;
    }
    return true;
}

bool ensure_switch_opened(AelEmitter *out, SwitchCtx *sw, int first_case_line0, int *anon_sp, int *anon_stack) {
    if (!out || !sw || !sw->active) return false;
    if (sw->opened) return true;
    if (!out->strict_pos) {
        if (!ael_emit_text(out, " {\n")) return false;
        sw->opened = true;
        if (anon_sp && anon_stack) {
            (void)anon_depth_push_marked(anon_sp, anon_stack, sw->depth + 1, true);
        }
        return true;
    }
    if (first_case_line0 == sw->line0 + 1) {
        if (!ael_emit_text(out, " {\n")) return false;
    } else {
        if (!ael_emit_text(out, "\n")) return false;
        if (!ael_emit_at(out, sw->line0 + 1, sw->col0)) return false;
        if (!ael_emit_text(out, "{\n")) return false;
    }
    sw->opened = true;
    if (anon_sp && anon_stack) {
        (void)anon_depth_push_marked(anon_sp, anon_stack, sw->depth + 1, true);
    }
    return true;
}

bool looks_like_inline_break_after_stmt_end(const IRProgram *program, size_t stmt_end_i, int line0) {
    if (!program) return false;
    size_t j = stmt_end_i + 1;
    if (j + 2 >= program->count) return false;
    const IRInst *a = &program->insts[j + 0];
    const IRInst *b = &program->insts[j + 1];
    const IRInst *c = &program->insts[j + 2];
    if (a->op != OP_LOAD_TRUE) return false;
    if (b->op != OP_LOOP_EXIT) return false;

    /* Some templates insert an ADD_LABEL between LOOP_EXIT and BRANCH_TRUE; some don't. */
    if (c->op == OP_BRANCH_TRUE) {
        if (!c->has_arg2 || !c->has_arg3) return false;
        return (c->arg2 == line0);
    }
    if (j + 3 >= program->count) return false;
    const IRInst *d = &program->insts[j + 3];
    if (c->op != OP_ADD_LABEL) return false;
    if (d->op != OP_BRANCH_TRUE || !d->has_arg2 || !d->has_arg3) return false;
    return (d->arg2 == line0);
}

bool op0_is_short_circuit_marker(const IRProgram *program, size_t idx, size_t end) {
    if (!program) return false;
    if (end > program->count) end = program->count;
    size_t next = ir_skip_scope_bookkeeping_end(program, idx + 1, end);
    if (next >= end) return false;
    const IRInst *n = &program->insts[next];
    return (n->op == OP_OP && n->has_arg1 && (n->arg1 == 62 || n->arg1 == 63));
}

bool begin_loop_has_for_scaffold(const IRProgram *program, size_t begin_i, int start_label) {
    if (!program) return false;
    size_t end = begin_i + 64;
    if (end > program->count) end = program->count;
    for (size_t j = begin_i + 1; j + 6 < end; j++) {
        const IRInst *a = &program->insts[j];
        if (a->op == OP_BEGIN_FUNCT || a->op == OP_DEFINE_FUNCT || a->op == OP_END_LOOP) break;
        if (a->op == OP_NUM_LOCAL || a->op == OP_DROP_LOCAL) continue;
        if (a->op != OP_BRANCH_TRUE || !a->has_arg1) continue;
        if (start_label >= 0 && a->arg1 == start_label) continue;

        /* Require the header-style ADD_LABEL immediately before the branch. */
        size_t prev = ir_skip_locals_bookkeeping_back(program, j);
        if (prev == 0 || program->insts[prev - 1].op != OP_ADD_LABEL) continue;

        size_t k = ir_skip_locals_bookkeeping(program, j + 1);
        if (k >= end || program->insts[k].op != OP_LOAD_TRUE) continue;
        k = ir_skip_locals_bookkeeping(program, k + 1);
        if (k >= end || program->insts[k].op != OP_LOOP_EXIT) continue;
        k = ir_skip_locals_bookkeeping(program, k + 1);
        if (k < end && program->insts[k].op == OP_ADD_LABEL) {
            k = ir_skip_locals_bookkeeping(program, k + 1);
        }
        if (k >= end || program->insts[k].op != OP_BRANCH_TRUE || !program->insts[k].has_arg1) continue;
        k = ir_skip_locals_bookkeeping(program, k + 1);
        if (k >= end || program->insts[k].op != OP_LOOP_AGAIN) continue;
        return true;
    }
    return false;
}

bool switch_emit_pending_case_before_stmt(AelEmitter *out, SwitchCtx *sw, int stmt_line0, int stmt_col0, int *anon_sp, int *anon_stack) {
    if (!out || !sw || !sw->active) return true;
    if (sw->pending_case_emitted || sw->pending_case_kind == 0) return true;

    int case_col0 = out->strict_pos ? (sw->col0 + 4) : decl_indent_col0_from_depth(sw->depth + 1);
    bool inline_case = out->strict_pos ? (stmt_col0 >= case_col0 + 8) : false;

    int case_line0 = inline_case ? stmt_line0 : (stmt_line0 - 1);
    if (!out->strict_pos) {
        case_line0 = out->line0;
        if (out->col0 != 0) {
            if (!ael_emit_char(out, '\n')) return false;
        }
    } else {
        if (case_line0 < sw->line0 + 1) case_line0 = sw->line0 + 1;
    }
    if (!ensure_switch_opened(out, sw, case_line0, anon_sp, anon_stack)) return false;
    if (case_line0 < out->line0) case_line0 = out->line0;
    if (!ael_emit_at(out, case_line0, case_col0)) return false;

    if (sw->pending_case_kind == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), inline_case ? "case %d: " : "case %d:\n", sw->pending_case_value);
        if (!ael_emit_text(out, buf)) return false;
    } else {
        if (!ael_emit_text(out, inline_case ? "default: " : "default:\n")) return false;
    }

    sw->pending_case_emitted = true;
    sw->pending_case_kind = 0;
    return true;
}

bool switch_emit_pending_case_label_only(AelEmitter *out, SwitchCtx *sw, int *anon_sp, int *anon_stack) {
    if (!out || !sw || !sw->active) return true;
    if (sw->pending_case_emitted || sw->pending_case_kind == 0) return true;

    int stmt_line0 = out->line0 + 1;
    int stmt_col0 = out->strict_pos ? (sw->col0 + 4) : 0;
    return switch_emit_pending_case_before_stmt(out, sw, stmt_line0, stmt_col0, anon_sp, anon_stack);
}

bool switch_is_epilogue_branch(const IRProgram *program, size_t branch_i, const SwitchCtx *sw) {
    if (!program || !sw || !sw->active || !sw->has_end_label) return false;
    if (branch_i + 2 >= program->count) return false;

    /*
     * Baseline switch epilogue pattern (from parse_switch_statement):
     *   LOAD_TRUE, LOOP_EXIT, BRANCH_TRUE end_label,
     *   LOOP_AGAIN, SET_LABEL table_label, BRANCH_TABLE,
     *   LOOP_EXIT, SET_LABEL end_label, END_LOOP
     *
     * We must not re-emit that BRANCH_TRUE as "break;". This detector is position-independent.
     */
    size_t j = branch_i + 1;
    if (program->insts[j].op != OP_LOOP_AGAIN) return false;
    if (j + 2 >= program->count) return false;
    const IRInst *set = &program->insts[j + 1];
    const IRInst *bt = &program->insts[j + 2];
    if (set->op != OP_SET_LABEL || !set->has_arg1) return false;
    if (bt->op != OP_BRANCH_TABLE) return false;
    if (sw->table_label >= 0 && set->arg1 != sw->table_label) return false;

    for (size_t k = j + 3; k + 2 < program->count && k < j + 12; k++) {
        if (program->insts[k].op == OP_LOOP_EXIT &&
            program->insts[k + 1].op == OP_SET_LABEL && program->insts[k + 1].has_arg1 &&
            program->insts[k + 1].arg1 == sw->end_label &&
            program->insts[k + 2].op == OP_END_LOOP) {
            return true;
        }
    }
    return false;
}

bool scan_for_if_header_line(const IRProgram *program, size_t start, size_t max_scan, int line0) {
    if (!program) return false;
    size_t end = start + max_scan;
    if (end > program->count) end = program->count;
    for (size_t j = start; j + 3 < end; j++) {
        const IRInst *a = &program->insts[j];
        const IRInst *b = &program->insts[j + 1];
        const IRInst *c = &program->insts[j + 2];
        const IRInst *d = &program->insts[j + 3];
        if (a->op == OP_OP && a->has_arg1 && a->arg1 == 59 && a->has_arg2 && a->arg2 == line0 &&
            b->op == OP_ADD_LABEL &&
            c->op == OP_OP && c->has_arg1 && c->arg1 == 3 &&
            d->op == OP_BRANCH_TRUE && d->has_arg1) {
            return true;
        }
    }
    return false;
}

bool scan_for_if_header_any(const IRProgram *program, size_t start, size_t max_scan, int *out_line0) {
    if (out_line0) *out_line0 = -1;
    if (!program) return false;
    size_t end = start + max_scan;
    if (end > program->count) end = program->count;
    for (size_t j = start; j + 3 < end; j++) {
        const IRInst *a = &program->insts[j];
        if (!(a->op == OP_OP && a->has_arg1 && a->arg1 == 59)) continue;

        size_t k = j + 1;
        for (; k < end; k++) {
            const IRInst *mj = &program->insts[k];
            if (mj->op == OP_NUM_LOCAL || mj->op == OP_DROP_LOCAL) continue;
            if (mj->op == OP_OP && mj->has_arg1 && (mj->arg1 == 60 || mj->arg1 == 61)) continue;
            break;
        }
        if (k >= end || program->insts[k].op != OP_ADD_LABEL) continue;
        k++;

        for (; k < end; k++) {
            const IRInst *mj = &program->insts[k];
            if (mj->op == OP_NUM_LOCAL || mj->op == OP_DROP_LOCAL) continue;
            if (mj->op == OP_OP && mj->has_arg1 && (mj->arg1 == 60 || mj->arg1 == 61)) continue;
            break;
        }
        if (k >= end) continue;

        /* Accept both plain and short-circuit if-header tails:
           - OP=3; BRANCH_TRUE
           - OP=62/63; OP=36; [OP=3]; BRANCH_TRUE */
        if (program->insts[k].op == OP_OP && program->insts[k].has_arg1 &&
            (program->insts[k].arg1 == 62 || program->insts[k].arg1 == 63)) {
            int marker = program->insts[k].arg1;
            k++;
            k = ir_skip_scope_bookkeeping_end(program, k, end);
            if (k >= end || !(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 36)) continue;
            k++;
            k = ir_skip_scope_bookkeeping_end(program, k, end);
            if (marker == 62) {
                if (k >= end || !(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 3)) continue;
                k++;
                k = ir_skip_scope_bookkeeping_end(program, k, end);
            }
            if (k >= end || !(program->insts[k].op == OP_BRANCH_TRUE && program->insts[k].has_arg1)) continue;
        } else {
            if (!(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 3)) continue;
            k++;

            for (; k < end; k++) {
                const IRInst *mj = &program->insts[k];
                if (mj->op == OP_NUM_LOCAL || mj->op == OP_DROP_LOCAL) continue;
                if (mj->op == OP_OP && mj->has_arg1 && (mj->arg1 == 60 || mj->arg1 == 61)) continue;
                break;
            }
            if (k >= end || !(program->insts[k].op == OP_BRANCH_TRUE && program->insts[k].has_arg1)) continue;
        }

        if (out_line0 && a->has_arg2) *out_line0 = a->arg2;
        return true;
    }
    return false;
}

bool scan_for_if_header_until_label(const IRProgram *program, size_t start, size_t max_scan, int stop_label, int expected_depth, int *out_line0) {
    if (out_line0) *out_line0 = -1;
    if (!program) return false;
    size_t end = start + max_scan;
    if (end > program->count) end = program->count;
    for (size_t j = start; j + 3 < end; j++) {
        const IRInst *a = &program->insts[j];
        if (stop_label >= 0 && a->op == OP_SET_LABEL && a->has_arg1 && a->arg1 == stop_label) break;
        if (a->op == OP_BEGIN_FUNCT || a->op == OP_DEFINE_FUNCT) break;
        if (!(a->op == OP_OP && a->has_arg1 && a->arg1 == 59)) continue;
        if (!(expected_depth < 0 || !a->has_depth || a->depth == expected_depth)) continue;

        size_t k = j + 1;
        for (; k < end; k++) {
            const IRInst *mj = &program->insts[k];
            if (stop_label >= 0 && mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == stop_label) break;
            if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
            if (mj->op == OP_NUM_LOCAL || mj->op == OP_DROP_LOCAL) continue;
            if (mj->op == OP_OP && mj->has_arg1 && (mj->arg1 == 60 || mj->arg1 == 61)) continue;
            break;
        }
        if (k >= end) continue;
        if (stop_label >= 0 && program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 && program->insts[k].arg1 == stop_label) break;
        if (program->insts[k].op != OP_ADD_LABEL) continue;
        k++;

        for (; k < end; k++) {
            const IRInst *mj = &program->insts[k];
            if (stop_label >= 0 && mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == stop_label) break;
            if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
            if (mj->op == OP_NUM_LOCAL || mj->op == OP_DROP_LOCAL) continue;
            if (mj->op == OP_OP && mj->has_arg1 && (mj->arg1 == 60 || mj->arg1 == 61)) continue;
            break;
        }
        if (k >= end) continue;
        if (stop_label >= 0 && program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 && program->insts[k].arg1 == stop_label) break;
        if (k >= end) continue;

        /* Accept plain or short-circuit if-header tails. */
        if (program->insts[k].op == OP_OP && program->insts[k].has_arg1 &&
            (program->insts[k].arg1 == 62 || program->insts[k].arg1 == 63)) {
            int marker = program->insts[k].arg1;
            k++;
            k = ir_skip_scope_bookkeeping_end(program, k, end);
            if (k >= end) continue;
            if (stop_label >= 0 && program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 && program->insts[k].arg1 == stop_label) break;
            if (!(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 36)) continue;
            k++;
            k = ir_skip_scope_bookkeeping_end(program, k, end);
            if (k >= end) continue;
            if (stop_label >= 0 && program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 && program->insts[k].arg1 == stop_label) break;
            if (marker == 62) {
                if (!(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 3)) continue;
                k++;
                k = ir_skip_scope_bookkeeping_end(program, k, end);
                if (k >= end) continue;
                if (stop_label >= 0 && program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 && program->insts[k].arg1 == stop_label) break;
            }
            if (!(program->insts[k].op == OP_BRANCH_TRUE && program->insts[k].has_arg1)) continue;
        } else {
            if (!(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 3)) continue;
            k++;

            for (; k < end; k++) {
                const IRInst *mj = &program->insts[k];
                if (stop_label >= 0 && mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == stop_label) break;
                if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
                if (mj->op == OP_NUM_LOCAL || mj->op == OP_DROP_LOCAL) continue;
                if (mj->op == OP_OP && mj->has_arg1 && (mj->arg1 == 60 || mj->arg1 == 61)) continue;
                break;
            }
            if (k >= end) continue;
            if (stop_label >= 0 && program->insts[k].op == OP_SET_LABEL && program->insts[k].has_arg1 && program->insts[k].arg1 == stop_label) break;
            if (!(program->insts[k].op == OP_BRANCH_TRUE && program->insts[k].has_arg1)) continue;
        }

        if (out_line0 && a->has_arg2) *out_line0 = a->arg2;
        return true;
    }
    return false;
}

bool ir_if_header_at(const IRProgram *program, size_t i, size_t end, int expected_depth, int *out_line0) {
    if (out_line0) *out_line0 = -1;
    if (!program) return false;
    if (end > program->count) end = program->count;
    if (i >= end) return false;
    const IRInst *a = &program->insts[i];
    if (!(a->op == OP_OP && a->has_arg1 && a->arg1 == 59)) return false;
    if (!(expected_depth < 0 || !a->has_depth || a->depth == expected_depth)) return false;

    size_t k = i + 1;
    k = ir_skip_scope_bookkeeping_end(program, k, end);
    if (k >= end || program->insts[k].op != OP_ADD_LABEL) return false;
    k++;
    k = ir_skip_scope_bookkeeping_end(program, k, end);
    if (k >= end) return false;

    if (program->insts[k].op == OP_OP && program->insts[k].has_arg1 &&
        (program->insts[k].arg1 == 62 || program->insts[k].arg1 == 63)) {
        int marker = program->insts[k].arg1;
        k++;
        k = ir_skip_scope_bookkeeping_end(program, k, end);
        if (k >= end || !(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 36)) return false;
        k++;
        k = ir_skip_scope_bookkeeping_end(program, k, end);
        if (k >= end) return false;
        if (marker == 62) {
            if (!(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 3)) return false;
            k++;
            k = ir_skip_scope_bookkeeping_end(program, k, end);
            if (k >= end) return false;
        }
        if (!(program->insts[k].op == OP_BRANCH_TRUE && program->insts[k].has_arg1)) return false;
    } else {
        if (!(program->insts[k].op == OP_OP && program->insts[k].has_arg1 && program->insts[k].arg1 == 3)) return false;
        k++;
        k = ir_skip_scope_bookkeeping_end(program, k, end);
        if (k >= end || !(program->insts[k].op == OP_BRANCH_TRUE && program->insts[k].has_arg1)) return false;
    }

    if (out_line0 && a->has_arg2) *out_line0 = a->arg2;
    return true;
}

bool ir_else_if_chain_at(const IRProgram *program, size_t i, size_t end, int expected_depth, int outer_end_label, int *out_line0) {
    if (!ir_if_header_at(program, i, end, expected_depth, out_line0)) return false;
    if (!program || outer_end_label < 0) return false;
    if (end > program->count) end = program->count;
    size_t scan_end = end;
    for (size_t j = i; j + 2 < scan_end; j++) {
        const IRInst *mj = &program->insts[j];
        if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
        if (mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == outer_end_label) break;
        if (mj->op == OP_LOAD_TRUE &&
            program->insts[j + 1].op == OP_BRANCH_TRUE && program->insts[j + 1].has_arg1 &&
            program->insts[j + 1].arg1 == outer_end_label &&
            program->insts[j + 2].op == OP_SET_LABEL) {
            return true;
        }
    }
    return false;
}

bool num_local_should_open_scope_block(const IRProgram *program, size_t i, int target_depth) {
    if (!program) return false;
    if (target_depth <= 1) return false;

    for (size_t j = i + 1; j < program->count && j < i + 256; j++) {
        const IRInst *mj = &program->insts[j];
        if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;

        /* If we drop below the target depth, we left this scope region. */
        if (mj->has_depth && mj->depth < target_depth) break;

        /* Evidence of a real source block-scope: locals declared at the new depth. */
        if (mj->op == OP_ADD_LOCAL && mj->has_depth && mj->depth == target_depth) {
            return true;
        }

        /* If we hit an anchored statement at/below the target depth before seeing ADD_LOCAL,
           treat it as bookkeeping (avoid opening braces that distort control-flow templates). */
        if (mj->op == OP_OP && mj->has_arg1 && mj->has_arg2 &&
            (mj->arg1 == 16 || mj->arg1 == 20 || mj->arg1 == 48 || mj->arg1 == 59)) {
            if (!mj->has_depth || mj->depth <= target_depth) {
                return false;
            }
        }
    }
    return false;
}

bool scan_for_assignment_to_var(const IRProgram *program, size_t start, size_t max_scan, const char *var_name, int depth, bool strict_depth) {
    if (!program || !var_name || !*var_name) return false;
    size_t end = start + max_scan;
    if (end > program->count) end = program->count;
    for (size_t i = start; i < end; i++) {
        const IRInst *a = &program->insts[i];
        if (a->op == OP_BEGIN_FUNCT || a->op == OP_DEFINE_FUNCT) break;
        if (strict_depth && a->has_depth && a->depth < depth) break;
        if (a->op != OP_LOAD_VAR || !a->str) continue;
        if (strcmp(a->str, var_name) != 0) continue;
        if (strict_depth && a->has_depth && a->depth != depth) continue;

        bool saw_assign = false;
        for (size_t j = i + 1; j < end && j < i + 48; j++) {
            const IRInst *b = &program->insts[j];
            if (b->op == OP_BEGIN_FUNCT || b->op == OP_DEFINE_FUNCT) break;
            if (strict_depth && b->has_depth && b->depth < depth) break;
            if (b->op == OP_OP && b->has_arg1 && b->arg1 == 16) saw_assign = true;
            if (saw_assign && b->op == OP_OP && b->has_arg1 && b->arg1 == 0) {
                return true;
            }
        }
    }
    return false;
}

bool find_for_header_cond_col0(const IRProgram *program, size_t start, size_t max_scan, int line0, int *out_col0) {
    if (out_col0) *out_col0 = -1;
    if (!program) return false;
    size_t end = start + max_scan;
    if (end > program->count) end = program->count;
    for (size_t j = start; j + 1 < program->count && j < end; j++) {
        const IRInst *mj = &program->insts[j];
        if (mj->op == OP_OP && mj->has_arg1 && mj->arg1 == 17 && mj->has_arg2 && mj->has_arg3 &&
            mj->arg2 == line0) {
            if (out_col0) *out_col0 = mj->arg3;
            return true;
        }
    }
    return false;
}

bool find_for_header_lparen_col0(const IRProgram *program, size_t start, size_t max_scan, int line0, int incr_label, int loop_end_label, int *out_col0) {
    if (out_col0) *out_col0 = -1;
    if (!program) return false;
    size_t end = start + max_scan;
    if (end > program->count) end = program->count;
    for (size_t j = start; j < program->count && j < end; j++) {
        const IRInst *mj = &program->insts[j];
        if (mj->op == OP_BEGIN_FUNCT || mj->op == OP_DEFINE_FUNCT) break;
        if (loop_end_label >= 0 && mj->op == OP_SET_LABEL && mj->has_arg1 && mj->arg1 == loop_end_label) break;
        if (mj->op == OP_BRANCH_TRUE && mj->has_arg1 && mj->arg1 == incr_label &&
            mj->has_arg2 && mj->has_arg3 && mj->arg2 == line0) {
            if (out_col0) *out_col0 = mj->arg3;
            return true;
        }
    }
    return false;
}
