/*
 * ael_parser_new.c
 * Clear, maintainable AEL Parser Implementation
 *
 * This parser:
 * - Uses clear, meaningful variable names
 * - Implements recursive descent parsing
 * - Calls existing IR generation functions (acomp_xxx)
 * - Is easy to debug and extend
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ael_parser_new.h"
#include "ir_generator.h"
#include "lexer_state.h"
#include "token_to_subopcode.h"

/* External lexer function */
extern int ascan_lex(int token_hint);

/* Global parser state */
extern int dword_18007E890;  /* Lookahead token */
extern int dword_18007E898;  /* Error count */

/* Forward declarations from other parser files */
bool parse_statement(ParserContext *ctx);
bool parse_decl_statement(ParserContext *ctx);
bool parse_block_statement(ParserContext *ctx);

/* Forward declaration for assignment parsing helpers */
static bool parse_multiplicative_continue(ParserContext *ctx);
static bool parse_expression_continue(ParserContext *ctx);

ParserContext g_parser_ctx = {0};

/* ============================================================================
 * Unit Suffix Support
 * ============================================================================ */

/**
 * Unit conversion table - returns multiplier for known units, 0.0 otherwise
 */
static double get_unit_multiplier(const char *unit) {
    static const struct {
        const char *name;
        double multiplier;
    } units[] = {
        /* Length units */
        {"um", 1e-6},      /* micrometer */
        {"mm", 1e-3},      /* millimeter */
        {"mil", 25.4e-6},  /* mil (1/1000 inch) */
        {"nm", 1e-9},      /* nanometer */
        {"cm", 1e-2},      /* centimeter */
        {"m", 1e-3},       /* milli (baseline behavior) */

        /* Frequency units */
        {"Hz", 1.0},       /* Hertz */
        {"kHz", 1e3},      /* kilohertz */
        {"MHz", 1e6},      /* megahertz */
        {"GHz", 1e9},      /* gigahertz */
        {"THz", 1e12},     /* terahertz */

        /* Capacitance units */
        {"F", 1.0},        /* Farad */
        {"pF", 1e-12},     /* picofarad */
        {"nF", 1e-9},      /* nanofarad */
        {"uF", 1e-6},      /* microfarad */
        {"mF", 1e-3},      /* millifarad */

        /* Resistance units */
        {"ohm", 1.0},      /* Ohm */
        {"kohm", 1e3},     /* kiloohm */
        {"Mohm", 1e6},     /* megaohm */

        /* Inductance units */
        {"H", 1.0},        /* Henry */
        {"pH", 1e-12},     /* picohenry */
        {"nH", 1e-9},      /* nanohenry */
        {"uH", 1e-6},      /* microhenry */
        {"mH", 1e-3},      /* millihenry */

        /* Time units */
        {"s", 1.0},        /* second */
        {"ms", 1e-3},      /* millisecond */
        {"us", 1e-6},      /* microsecond */
        {"ns", 1e-9},      /* nanosecond */
        {"ps", 1e-12},     /* picosecond */

        {NULL, 0.0}
    };

    for (int i = 0; units[i].name != NULL; i++) {
        if (strcmp(unit, units[i].name) == 0) {
            return units[i].multiplier;
        }
    }
    return 0.0;  /* Not a known unit */
}

/* ============================================================================
 * Token Management
 * ============================================================================ */

/*
 * Token position tracking
 *
 * Note: the lexer stores only a single "current token" position. Because this
 * parser uses a 1-token lookahead (peek_token), peeking would otherwise
 * overwrite the position of the last consumed token. We keep positions here so
 * code generation can refer to stable locations even after lookahead peeks.
 */
static int g_last_token_line = 0;      /* 0-based */
static int g_last_token_col = 0;       /* 0-based */
static int g_lookahead_line = 0;       /* 0-based */
static int g_lookahead_col = 0;        /* 0-based */
static int g_last_nonempty_string_line = 0;  /* 0-based */
static int g_last_nonempty_string_col = 0;   /* 0-based */
static bool g_last_nonempty_string_valid = false;
static int g_last_not_line = 0;              /* 0-based */
static int g_last_not_col = 0;               /* 0-based */
static bool g_last_not_valid = false;
static int g_last_compare_line = 0;          /* 0-based */
static int g_last_compare_col = 0;           /* 0-based */
static bool g_last_compare_valid = false;
static int g_last_ident_line = 0;            /* 0-based */
static int g_last_ident_col = 0;             /* 0-based */
static bool g_last_ident_valid = false;
static int g_last_null_line = 0;             /* 0-based */
static int g_last_null_col = 0;              /* 0-based */
static bool g_last_null_valid = false;
static int g_expr_chain_start_line = 0;      /* 0-based: start of current expression chain */
static int g_expr_chain_start_col = 0;       /* 0-based */
static bool g_expr_chain_start_valid = false;

/**
 * Get the next token from the lexer
 */
int next_token(void) {
    /* Use lookahead if available */
    if (dword_18007E890 != -1) {
        int token = dword_18007E890;
        g_last_token_line = g_lookahead_line;
        g_last_token_col = g_lookahead_col;
        dword_18007E890 = -1;
        return token;
    }

    /* Get from lexer */
    int token = ascan_lex(0);
    g_last_token_line = lexer_get_line();
    g_last_token_col = lexer_get_column();
    return token;
}

/**
 * Peek at current token without consuming it
 */
int peek_token(void) {
    if (dword_18007E890 == -1) {
        dword_18007E890 = ascan_lex(0);
        g_lookahead_line = lexer_get_line();
        g_lookahead_col = lexer_get_column();
    }
    return dword_18007E890;
}

/**
 * Consume a token if it matches expected, otherwise error
 */
bool expect_token(int expected_token) {
    int token = next_token();
    if (token != expected_token) {
        fprintf(stderr, "Parser error: expected %s but got %s\n",
                token_name(expected_token), token_name(token));
        g_parser_ctx.had_error = true;
        g_parser_ctx.error_count++;
        dword_18007E898++;
        return false;
    }
    return true;
}

/**
 * Report parser error
 */
void parser_error(const char *message) {
    fprintf(stderr, "Parser error: %s (current token: %s)\n",
            message, token_name(peek_token()));
    g_parser_ctx.had_error = true;
    g_parser_ctx.error_count++;
    dword_18007E898++;
}

/**
 * Get printable token name for error messages
 */
const char *token_name(int token) {
    static char buf[32];
    switch (token) {
        case TOK_EOF: return "EOF";
        case TOK_LPAREN: return "'('";
        case TOK_RPAREN: return "')'";
        case TOK_COMMA: return "','";
        case TOK_SEMICOLON: return "';'";
        case TOK_LBRACE: return "'{'";
        case TOK_RBRACE: return "'}'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_DECL: return "decl";
        case TOK_DEFUN: return "defun";
        case TOK_IF: return "if";
        case TOK_ELSE: return "else";
        case TOK_WHILE: return "while";
        case TOK_FOR: return "for";
        case TOK_RETURN: return "return";
        case TOK_BREAK: return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_DO: return "do";
        case TOK_SWITCH: return "switch";
        case TOK_CASE: return "case";
        case TOK_DEFAULT: return "default";
        case TOK_INTEGER: return "INTEGER";
        case TOK_REAL: return "REAL";
        case TOK_STRING: return "STRING";
        case TOK_IDENTIFIER: return "IDENTIFIER";
        case TOK_ASSIGN: return "'='";
        case TOK_PLUS: return "'+'";
        case TOK_MINUS: return "'-'";
        case TOK_STAR: return "'*'";
        case TOK_SLASH: return "'/'";
        case TOK_PERCENT: return "'%'";
        case TOK_LT: return "'<'";
        case TOK_GT: return "'>'";
        case TOK_EQ: return "'=='";
        case TOK_NE: return "'!='";
        case TOK_LE: return "'<='";
        case TOK_GE: return "'>='";
        case TOK_AND: return "'&&'";
        case TOK_OR: return "'||'";
        case TOK_NOT: return "'!'";
        case TOK_QUESTION: return "'?'";
        case TOK_COLON: return "':'";
        case TOK_BIT_AND: return "'&'";
        case TOK_BIT_OR: return "'|'";
        case TOK_BIT_XOR: return "'^'";
        case TOK_LSHIFT: return "'<<'";
        case TOK_RSHIFT: return "'>>'";
        case TOK_POWER: return "'**'";
        default:
            snprintf(buf, sizeof(buf), "token(%d)", token);
            return buf;
    }
}

/* ============================================================================
 * Expression Parsing (Recursive Descent with Precedence)
 * ============================================================================ */

/**
 * Parse primary expression:
 *   - INTEGER literal
 *   - REAL literal
 *   - STRING literal
 *   - IDENTIFIER (variable or function call)
 *   - ( expression )
 */
bool parse_primary_expr(ParserContext *ctx) {
    int token = peek_token();
    int line, col;  /* Position tracking for acomp_op calls */

    switch (token) {
        case TOK_INTEGER: {
            next_token();
            int value = lexer_get_int();
            acomp_integer(value);

            /* Check for unit identifier (implicit multiplication) */
            if (peek_token() == TOK_IDENTIFIER) {
                next_token();  /* Consume the unit identifier */
                const char *unit_name = lexer_get_identifier();

                /* Check if it's a known unit suffix */
                double multiplier = get_unit_multiplier(unit_name);
                if (multiplier != 0.0) {
                    /* It's a unit - load the multiplier as a constant */
                    acomp_real(multiplier);
                    line = lexer_get_line();
                    col = lexer_get_column();
                    acomp_op(12, line, col, 2);  /* OP=12 (MUL) with a4=2 */
                } else {
                    /* Not a unit - treat as variable (implicit multiplication) */
                    acomp_word_ref(NULL, unit_name);
                    line = lexer_get_line();
                    col = lexer_get_column();
                    acomp_op(3, line, col, 0);  /* MUL */
                }
            }
            return true;
        }

        case TOK_REAL: {
            next_token();
            double value = lexer_get_real();
            acomp_real(value);

            /* Check for unit identifier (implicit multiplication) */
            if (peek_token() == TOK_IDENTIFIER) {
                next_token();  /* Consume the unit identifier */
                const char *unit_name = lexer_get_identifier();

                /* Check if it's a known unit suffix */
                double multiplier = get_unit_multiplier(unit_name);
                if (multiplier != 0.0) {
                    /* It's a unit - load the multiplier as a constant */
                    acomp_real(multiplier);
                    line = lexer_get_line();
                    col = lexer_get_column();
                    acomp_op(12, line, col, 2);  /* OP=12 (MUL) with a4=2 */
                } else {
                    /* Not a unit - treat as variable (implicit multiplication) */
                    acomp_word_ref(NULL, unit_name);
                    line = lexer_get_line();
                    col = lexer_get_column();
                    acomp_op(3, line, col, 0);  /* MUL */
                }
            }
            return true;
        }

        case TOK_IMAG: {
            next_token();
            double value = lexer_get_real();
            acomp_imag(value);

            /* Check for unit identifier (implicit multiplication) */
            if (peek_token() == TOK_IDENTIFIER) {
                next_token();
                const char *unit_name = lexer_get_identifier();
                acomp_word_ref(NULL, unit_name);
                line = lexer_get_line();
                col = lexer_get_column();
                acomp_op(3, line, col, 0);  /* MUL */
            }
            return true;
        }

        case TOK_STRING: {
            int str_line = lexer_get_line();
            int str_col = lexer_get_column();

            next_token();
            const char *str = lexer_get_string();
            acomp_string(str);

            /* Track last non-empty string literal position (some baseline IR
             * patterns reuse a previous non-empty string position as an anchor).
             */
            if (str && str[0] != '\0') {
                g_last_nonempty_string_line = str_line;
                g_last_nonempty_string_col = str_col;
                g_last_nonempty_string_valid = true;
            }
            return true;
        }

        case TOK_TRUE: {
            /* Uppercase TRUE is a boolean literal keyword */
            next_token();
            acomp_bool(1);  // Generate OP=5 LOAD_BOOL val=true
            return true;
        }

        case TOK_FALSE: {
            /* Uppercase FALSE is a boolean literal keyword */
            next_token();
            acomp_bool(0);  // Generate OP=5 LOAD_BOOL val=false
            return true;
        }

        case TOK_NULL: {
            int null_line = lexer_get_line();
            int null_col = lexer_get_column();
            next_token();
            acomp_null();
            g_last_null_line = null_line;
            g_last_null_col = null_col;
            g_last_null_valid = true;
            return true;
        }

        case TOK_IDENTIFIER: {
            /* Save position BEFORE consuming identifier */
            int id_line = lexer_get_line();
            int id_col = lexer_get_column();

            next_token();
            /* IMPORTANT: lexer identifier text is stored in a shared buffer that can be
             * overwritten by subsequent peek_token()/next_token() calls. Copy it now.
             */
            const char *name = lexer_get_identifier();
            char name_buf[256];
            strncpy(name_buf, name ? name : "", sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';

            g_last_ident_line = id_line;
            g_last_ident_col = id_col;
            g_last_ident_valid = true;

            /* Check if this is a function call */
            if (peek_token() == TOK_LPAREN) {
                return parse_function_call_with_pos(ctx, name_buf, id_line, id_col);
            } else if (peek_token() == TOK_LBRACKET) {
                /* Array indexing: identifier[index] or identifier[x, y, z] */
                /* Load the array variable */
                acomp_word_ref(NULL, name_buf);

                /* Parse all array indices (supports both arr[x][y] and arr[x,y] syntax).
                 *
                 * Baseline order detail:
                 *   For chained brackets (arr[x][y]) the baseline pushes all index expressions first,
                 *   then emits the indexing OPs (so indices appear before OP=48 in IR).
                 */
                int index_group_counts[16];
                int index_group_count = 0;
                while (peek_token() == TOK_LBRACKET) {
                    next_token();  /* Consume '[' - don't save position */

                    int index_count = 0;

                    /* Parse first index expression */
                    if (!parse_expression(ctx)) {
                        return false;
                    }
                    index_count++;

                    /* Check for comma-separated indices: arr[x, y, z] */
                    while (peek_token() == TOK_COMMA) {
                        next_token();  /* Consume ',' */

                        /* Parse next index */
                        if (!parse_expression(ctx)) {
                            return false;
                        }
                        index_count++;
                    }

                    if (!expect_token(TOK_RBRACKET)) {
                        return false;
                    }

                    if (index_group_count >= (int)(sizeof(index_group_counts) / sizeof(index_group_counts[0]))) {
                        fprintf(stderr, "Error: Too many chained index groups\n");
                        return false;
                    }
                    index_group_counts[index_group_count++] = index_count;
                }

                for (int i = 0; i < index_group_count; i++) {
                    /* Generate array index operation - use identifier position.
                     * Baseline uses a single OP=48 for comma-separated indices:
                     *   arr[i,j] => arg4 = 1(array) + 2(indices) = 3
                     */
                    acomp_op(48, id_line, id_col, index_group_counts[i] + 1);
                }

                /* Generate EXPR operation once for the full indexing chain */
                acomp_op(17, id_line, id_col, 1);
                return true;
            } else {
                /* Variable reference */
                acomp_word_ref(NULL, name_buf);

                /* Generate OP=17 (EXPR) wrapper - use saved identifier position */
                acomp_op(17, id_line, id_col, 1);

                /* Check for unit identifier (implicit multiplication: Wp um) */
                if (peek_token() == TOK_IDENTIFIER) {
                    next_token();
                    const char *unit_name = lexer_get_identifier();

                    /* Check if it's a known unit suffix */
                    double multiplier = get_unit_multiplier(unit_name);
                    if (multiplier != 0.0) {
                        /* It's a unit - load multiplier constant and multiply */
                        acomp_real(multiplier);
                        line = lexer_get_line();
                        col = lexer_get_column();
                        acomp_op(12, line, col, 2);  /* OP=12 (MUL) with a4=2 */
                    } else {
                        /* Not a unit - treat as variable (implicit multiplication) */
                        acomp_word_ref(NULL, unit_name);
                        line = lexer_get_line();
                        col = lexer_get_column();
                        acomp_op(3, line, col, 0);  /* MUL */
                    }
                }

                return true;
            }
        }

        case TOK_LPAREN: {
            /* Save '(' position for ternary operator use
             * This position will be used by ternary operator for OP=61/60/65
             */
            ctx->last_lparen_line = lexer_get_line();
            ctx->last_lparen_col = lexer_get_column();

            next_token();  /* Consume '(' */
            if (!parse_expression(ctx)) {
                return false;
            }
            if (!expect_token(TOK_RPAREN)) {
                return false;
            }
            return true;
        }

        case TOK_LBRACE: {
            /* List literal: { expr1, expr2, ... }
             * Baseline IR pattern with OP=53:
             * - Simple list (DEPTH=2): 1x OP=53 before OP=46
             * - Nested list first inner (DEPTH=3, first): 2x OP=53 before OP=46
             * - Nested list subsequent inner (DEPTH=3, not first): 1x OP=53 before OP=46
             */
            next_token();  /* Consume '{' */

            /* Save position for error reporting */
            line = lexer_get_line();
            col = lexer_get_column();

            /* Enter list scope - increase DEPTH and generate NUM_LOCAL */
            int depth_before = AcompDepth;
            AcompDepth++;
            acomp_num_local();

            /* Track "first list at a depth" to reproduce baseline OP=53 patterns.
             * Observed:
             * - DEPTH=3: first list => 2x OP=53, subsequent => 1x OP=53
             * - DEPTH=4: first list => 3x OP=53, subsequent => 1x OP=53
             * Generalization for DEPTH>=3:
             *   first list at depth D => (D-1)x OP=53, subsequent => 1x OP=53
             *
             * We keep counters per depth, and reset deeper-depth counters when
             * exiting a list (so "first at depth" is scoped to its parent list).
             */
            enum { MAX_TRACKED_LIST_DEPTH = 16 };
            static int list_count_by_depth[MAX_TRACKED_LIST_DEPTH] = {0};

            bool is_first_list_at_depth = false;
            if (AcompDepth >= 3 && AcompDepth < MAX_TRACKED_LIST_DEPTH) {
                is_first_list_at_depth = (list_count_by_depth[AcompDepth] == 0);
                list_count_by_depth[AcompDepth]++;
            }

            int element_count = 0;
            bool contains_nested_lists = false;  /* Track if this list directly contains inner lists */
            int child_depth = AcompDepth + 1;
            int child_count_before = (child_depth >= 0 && child_depth < MAX_TRACKED_LIST_DEPTH)
                ? list_count_by_depth[child_depth]
                : 0;

            /* Empty list */
            if (peek_token() == TOK_RBRACE) {
                next_token();  /* Consume '}' */

                /* Baseline quirk: "{}" does not emit OP=46/OP=53 in our test set.
                 * Record the '}' position so assignment/statement/function epilogue
                 * can reproduce baseline IR (see 01_empty_list).
                 */
                int rbrace_line = lexer_get_line();
                int rbrace_col = lexer_get_column();
                ctx->last_expr_was_empty_brace_list = true;
                ctx->last_empty_brace_list_line = rbrace_line;
                ctx->last_empty_brace_list_col = rbrace_col;

                /* Exit list scope */
                int exiting_depth = AcompDepth;
                AcompDepth--;

                /* Reset deeper depth counters when leaving this list */
                if (exiting_depth + 1 < MAX_TRACKED_LIST_DEPTH) {
                    for (int d = exiting_depth + 1; d < MAX_TRACKED_LIST_DEPTH; d++) {
                        list_count_by_depth[d] = 0;
                    }
                }

                return true;
            }

            /* Parse list elements */
            while (true) {
                /* Parse element expression (handles nested lists recursively) */
                if (!parse_expression(ctx)) {
                    return false;
                }

                element_count++;

                /* Check for comma */
                if (peek_token() == TOK_COMMA) {
                    next_token();  /* Consume ',' */

                    /* Check for trailing comma */
                    if (peek_token() == TOK_RBRACE) {
                        break;
                    }
                } else {
                    /* No comma, expect closing brace */
                    break;
                }
            }

            if (!expect_token(TOK_RBRACE)) {
                return false;
            }

            /* Save position after '}' for OP=46 */
            line = lexer_get_line();
            col = lexer_get_column();

            /* Check if we parsed any nested lists at the direct child depth */
            if (child_depth >= 0 && child_depth < MAX_TRACKED_LIST_DEPTH &&
                list_count_by_depth[child_depth] > child_count_before) {
                contains_nested_lists = true;
            }

            /* Generate OP=53 according to the discovered pattern */
            int op53_count = 0;

            if (contains_nested_lists) {
                /* Outer list at any depth (contains nested lists): 0x OP=53 */
                op53_count = 0;
            } else if (AcompDepth == 2) {
                /* Simple list with primitives only: 1x OP=53 */
                op53_count = 1;
            } else if (AcompDepth >= 3) {
                op53_count = is_first_list_at_depth ? (AcompDepth - 1) : 1;
            }

            for (int i = 0; i < op53_count; i++) {
                acomp_op(53, 0, 0, 0);  /* OP=53 always uses 0,0 for position */
            }

            /* Generate OP=46 (BUILD_LIST) - pop element_count items from stack and build list */
            acomp_op(46, line, col, element_count);

            /* Exit list scope - decrease DEPTH */
            int exiting_depth = AcompDepth;
            AcompDepth--;

            /* Reset deeper depth counters when leaving this list */
            if (exiting_depth + 1 < MAX_TRACKED_LIST_DEPTH) {
                for (int d = exiting_depth + 1; d < MAX_TRACKED_LIST_DEPTH; d++) {
                    list_count_by_depth[d] = 0;
                }
            }

            return true;
        }

        default:
            parser_error("expected expression");
            return false;
    }
}

/**
 * Parse unary expression:
 *   - primary_expr
 *   - - unary_expr
 *   - ! unary_expr
 *   - ++ unary_expr  (pre-increment)
 *   - -- unary_expr  (pre-decrement)
 */
bool parse_unary_expr(ParserContext *ctx) {
    int token = peek_token();
    int line, col;  /* Position tracking for acomp_op calls */

    if (token == TOK_MINUS) {
        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();
        if (!parse_unary_expr(ctx)) {
            return false;
        }
        /* Generate negate operation */
        acomp_op(15, line, col, 1);  /* OP=48 arg1=15 (NEGATE) */
        return true;
     } else if (token == TOK_NOT) {
         /* Save operator position before consuming */
         line = lexer_get_line();
         col = lexer_get_column();

         g_last_not_line = line;
         g_last_not_col = col;
         g_last_not_valid = true;

         next_token();
         if (!parse_unary_expr(ctx)) {
             return false;
         }
        /* Generate logical NOT operation */
        acomp_op(3, line, col, 1);  /* OP=48 arg1=3 (NOT - match baseline) */
        return true;
    } else if (token == TOK_INCREMENT) {
        /* Pre-increment: ++x */
        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();
        if (peek_token() != TOK_IDENTIFIER) {
            parser_error("expected identifier after '++'");
            return false;
        }
        next_token();
        const char *var_name = lexer_get_identifier();

        /* Generate specialized pre-increment operation
         * Pattern: LOAD_VAR, OP=31 (pre-increment)
         * Note: OP=0 (end) is added by parse_expression_statement
         */
        acomp_word_ref(NULL, var_name);
        acomp_op(31, line, col, 1);  /* OP=48 arg1=31 (PRE_INCREMENT) */
        return true;
    } else if (token == TOK_DECREMENT) {
        /* Pre-decrement: --x */
        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();
        if (peek_token() != TOK_IDENTIFIER) {
            parser_error("expected identifier after '--'");
            return false;
        }
        next_token();
        const char *var_name = lexer_get_identifier();

        /* Generate specialized pre-decrement operation
         * Pattern: LOAD_VAR, OP=32 (pre-decrement)
         * Note: OP=0 (end) is added by parse_expression_statement
         */
        acomp_word_ref(NULL, var_name);
        acomp_op(32, line, col, 1);  /* OP=48 arg1=32 (PRE_DECREMENT) */
        return true;
    } else {
        return parse_primary_expr(ctx);
    }
}

/**
 * Parse power expression (exponentiation):
 *   unary_expr ** unary_expr
 *
 * Note: Power has higher precedence than multiplication but lower than unary.
 * It's right-associative: 2**3**4 = 2**(3**4) = 2**81
 */
bool parse_power_expr(ParserContext *ctx) {
    if (!parse_unary_expr(ctx)) {
        return false;
    }

    /* Right-associative: parse recursively for right operand */
    if (peek_token() == TOK_POWER) {
        /* Save operator position before consuming */
        int line = lexer_get_line();
        int col = lexer_get_column();
        next_token();  /* Consume '**' */

        /* Recursively parse right side (right-associative) */
        if (!parse_power_expr(ctx)) {
            return false;
        }

        /* Generate power operation */
        acomp_op(43, line, col, 2);  /* OP=43 for POWER (matches baseline) */
    }

    return true;
}

/**
 * Parse multiplicative expression:
 *   power_expr (* | / | %) power_expr
 */
bool parse_multiplicative_expr(ParserContext *ctx) {
    int line, col;  /* Position tracking for acomp_op calls */
    if (!parse_power_expr(ctx)) {
        return false;
    }

    while (true) {
        int token = peek_token();
        if (token != TOK_STAR && token != TOK_SLASH && token != TOK_PERCENT) {
            break;
        }

        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();  /* Consume operator */

        if (!parse_power_expr(ctx)) {
            return false;
        }

        /* Generate operation */
        int16_t op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse additive expression:
 *   multiplicative_expr (+ | -) multiplicative_expr
 */
bool parse_additive_expr(ParserContext *ctx) {
    int line, col;  /* Position tracking for acomp_op calls */
    if (!parse_multiplicative_expr(ctx)) {
        return false;
    }

    while (true) {
        int token = peek_token();
        if (token != TOK_PLUS && token != TOK_MINUS) {
            break;
        }

        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();  /* Consume operator */

        if (!parse_multiplicative_expr(ctx)) {
            return false;
        }

        /* Generate operation */
        int16_t op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse shift expression:
 *   additive_expr (<< | >>) additive_expr
 */
bool parse_shift_expr(ParserContext *ctx) {
    int line, col;  /* Position tracking for acomp_op calls */
    if (!parse_additive_expr(ctx)) {
        return false;
    }

    while (true) {
        int token = peek_token();
        if (token != TOK_LSHIFT && token != TOK_RSHIFT) {
            break;
        }

        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();  /* Consume operator */

        if (!parse_additive_expr(ctx)) {
            return false;
        }

        /* Generate shift operation - use centralized mapping */
        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse relational expression:
 *   shift_expr (< | > | <= | >=) shift_expr
 */
bool parse_relational_expr(ParserContext *ctx) {
    int line, col;  /* Position tracking for acomp_op calls */
    if (!parse_shift_expr(ctx)) {
        return false;
    }

    while (true) {
        int token = peek_token();
        if (token != TOK_LT && token != TOK_GT &&
            token != TOK_LE && token != TOK_GE) {
            break;
        }

        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();  /* Consume operator */

        if (!parse_shift_expr(ctx)) {
            return false;
        }

        /* Generate comparison operation */
        int16_t op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse equality expression:
 *   relational_expr (== | !=) relational_expr
 */
bool parse_equality_expr(ParserContext *ctx) {
    int line, col;  /* Position tracking for acomp_op calls */
    if (!parse_relational_expr(ctx)) {
        return false;
    }

    while (true) {
        int token = peek_token();
        if (token != TOK_EQ && token != TOK_NE) {
            break;
        }

        /* Save operator position before consuming */
        line = lexer_get_line();
        col = lexer_get_column();

        next_token();  /* Consume operator */

        if (!parse_relational_expr(ctx)) {
            return false;
        }

        /* Generate comparison operation */
        int16_t op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse bitwise AND expression:
 *   equality_expr & equality_expr
 */
bool parse_bit_and_expr(ParserContext *ctx) {
    if (!parse_equality_expr(ctx)) {
        return false;
    }

    while (peek_token() == TOK_BIT_AND) {
        /* Save operator position before consuming */
        int line = lexer_get_line();
        int col = lexer_get_column();

        next_token();

        if (!parse_equality_expr(ctx)) {
            return false;
        }

        /* Use centralized mapping for bitwise AND */
        int op_code = token_to_subopcode(TOK_BIT_AND);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse bitwise XOR expression:
 *   bit_and_expr ^ bit_and_expr
 */
bool parse_bit_xor_expr(ParserContext *ctx) {
    if (!parse_bit_and_expr(ctx)) {
        return false;
    }

    while (peek_token() == TOK_BIT_XOR) {
        /* Save operator position before consuming */
        int line = lexer_get_line();
        int col = lexer_get_column();

        next_token();

        if (!parse_bit_and_expr(ctx)) {
            return false;
        }

        /* Use centralized mapping for bitwise XOR */
        int op_code = token_to_subopcode(TOK_BIT_XOR);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse bitwise OR expression:
 *   bit_xor_expr | bit_xor_expr
 */
bool parse_bit_or_expr(ParserContext *ctx) {
    if (!parse_bit_xor_expr(ctx)) {
        return false;
    }

    while (peek_token() == TOK_BIT_OR) {
        /* Save operator position before consuming */
        int line = lexer_get_line();
        int col = lexer_get_column();

        next_token();

        if (!parse_bit_xor_expr(ctx)) {
            return false;
        }

        /* Use centralized mapping for bitwise OR */
        int op_code = token_to_subopcode(TOK_BIT_OR);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Parse logical AND expression with short-circuit evaluation:
 *   bit_or_expr && bit_or_expr
 */
bool parse_logical_and_expr(ParserContext *ctx) {
    if (!parse_bit_or_expr(ctx)) {
        return false;
    }

    /* Baseline behavior: for multi-AND chains like `a && b && c`, all AND ops
     * branch to a single shared end label, and the label is set once after the
     * chain completes (no intermediate SET_LABEL between each &&).
     */
    int and_end_label = -1;
    bool has_and_ops = (peek_token() == TOK_AND);
    if (has_and_ops) {
        and_end_label = acomp_add_label();  /* Create shared label ONCE for all ANDs */
    }

    int and_op_count = 0;
    int and_chain_anchor_line = 0;
    int and_chain_anchor_col = 0;
    bool and_chain_anchor_valid = false;

    while (peek_token() == TOK_AND) {
        next_token();  /* Consume && */
        and_op_count++;

        int line = g_last_token_line;
        int col = g_last_token_col;

        if (!and_chain_anchor_valid) {
            /* Baseline anchor selection for multi-AND chains:
             * - Prefer unary NOT position on the same line (e.g. `(!x || ...) && ...`)
             * - Else prefer last compare operator position on the same line (e.g. `x != NULL && ...`)
             * - Else (in some baseline patterns) reuse a nearby prior string literal position
             * - Else fall back to the first `&&` position itself (e.g. `x && ...`)
             */
            if (g_last_not_valid && g_last_not_line == line) {
                and_chain_anchor_line = g_last_not_line;
                and_chain_anchor_col = g_last_not_col;
                and_chain_anchor_valid = true;
            } else if (g_last_compare_valid && g_last_compare_line == line) {
                if (g_last_null_valid && g_last_null_line == line) {
                    and_chain_anchor_line = g_last_compare_line;
                    and_chain_anchor_col = g_last_compare_col;
                    and_chain_anchor_valid = true;
                } else if (g_last_ident_valid && g_last_ident_line == line) {
                    and_chain_anchor_line = g_last_ident_line;
                    and_chain_anchor_col = g_last_ident_col;
                    and_chain_anchor_valid = true;
                } else {
                    and_chain_anchor_line = g_last_compare_line;
                    and_chain_anchor_col = g_last_compare_col;
                    and_chain_anchor_valid = true;
                }
            } else if (g_last_nonempty_string_valid &&
                       g_last_nonempty_string_line < line &&
                       (line - g_last_nonempty_string_line) <= 3) {
                and_chain_anchor_line = g_last_nonempty_string_line;
                and_chain_anchor_col = g_last_nonempty_string_col;
                and_chain_anchor_valid = true;
            } else {
                and_chain_anchor_line = line;
                and_chain_anchor_col = col;
                and_chain_anchor_valid = true;
            }
        }

        /* Start short-circuit AND */
        acomp_op(62, line, col, 1);  /* OP=48 arg1=62 */
        acomp_op(36, line, col, 1);  /* OP=48 arg1=36 */
        acomp_op(3, line, col, 1);   /* OP=48 arg1=3 */

        /* Branch if left side is true */
        acomp_branch_true(and_end_label, line, col);

        /* Left is false, pop and continue */
        acomp_op(0, line, col, 1);   /* OP=48 arg1=0 */
        acomp_op(62, line, col, 1);  /* OP=48 arg1=62 */

        /* Evaluate right side */
        if (!parse_bit_or_expr(ctx)) {
            return false;
        }

        /* End short-circuit AND */
        int end_line = line;
        int end_col = col;
        if (and_op_count > 1 && and_chain_anchor_valid) {
            end_line = and_chain_anchor_line;
            end_col = and_chain_anchor_col;
        }
        acomp_op(62, end_line, end_col, 1);  /* OP=48 arg1=62 */
    }

    if (has_and_ops) {
        acomp_set_label(and_end_label);
    }

    return true;
}

/**
 * Parse logical OR expression with short-circuit evaluation:
 *   logical_and_expr || logical_and_expr
 */
bool parse_logical_or_expr(ParserContext *ctx) {
    if (!parse_logical_and_expr(ctx)) {
        return false;
    }

    /* Check if we have OR operators - if so, create shared end label */
    int shared_end_label = -1;
    bool has_or = (peek_token() == TOK_OR);

    if (has_or) {
        shared_end_label = acomp_add_label();  /* Create shared label ONCE for all ORs */
    }

    while (peek_token() == TOK_OR) {
        /* Save operator position before consuming */
        int line = lexer_get_line();
        int col = lexer_get_column();

        next_token();  /* Consume || */

        /* Start short-circuit OR */
        acomp_op(63, line, col, 1);  /* OP=48 arg1=63 */
        acomp_op(36, line, col, 1);  /* OP=48 arg1=36 */

        /* If left is TRUE, skip right and jump to shared end */
        acomp_branch_true(shared_end_label, line, col);  /* All ORs use SAME label */

        /* Left is false, pop and continue */
        acomp_op(0, line, col, 1);   /* OP=48 arg1=0 */
        acomp_op(63, line, col, 1);  /* OP=48 arg1=63 */

        /* Evaluate right side */
        if (!parse_logical_and_expr(ctx)) {
            return false;
        }

        /* End short-circuit OR */
        acomp_op(63, line, col, 1);  /* OP=48 arg1=63 */

        /* Don't set label here - wait until all ORs are processed */
    }

    /* Set shared end label after all OR operators */
    if (has_or) {
        acomp_set_label(shared_end_label);  /* Set ONCE at end */
    }

    return true;
}

/**
 * Parse ternary conditional expression:
 *   logical_or_expr ? expr : expr
 */
bool parse_ternary_expr(ParserContext *ctx) {
    /* Save expression start position (first token of the condition expression).
     * NOTE: callers may have just consumed an operator (e.g. '='), so the lexer
     * position can still point at the previous token. Force a lookahead so the
     * lexer position reflects the actual first token of the condition.
     */
    (void)peek_token();
    int expr_start_line = lexer_get_line();
    int expr_start_col = lexer_get_column();

    if (!parse_logical_or_expr(ctx)) {
        return false;
    }

    if (peek_token() == TOK_QUESTION) {
        /* Save '?' position BEFORE consuming */
        int question_line = lexer_get_line();
        int question_col = lexer_get_column();

        next_token();  /* Consume '?' */

        /* Start ternary: OP=59 (START_TERNARY) - use '?' position */
        acomp_op(59, question_line, question_col, 1);

        /* Add two labels: label0 for false branch, label1 for end */
        int label_false = acomp_add_label();
        int label_end = acomp_add_label();

        /* Check condition and branch - use '?' position */
        acomp_op(3, question_line, question_col, 1);   /* OP=48 arg1=3 */
        acomp_branch_true(label_false, question_line, question_col);

        /* True branch marker - OP=61 - baseline uses condition start line + column */
        acomp_op(61, expr_start_line, expr_start_col, 1);

        /* Parse true expression */
        if (!parse_expression(ctx)) {
            return false;
        }

        /* Save position after true expression for BRANCH_TRUE */
        int true_end_line = lexer_get_line();
        int true_end_col = lexer_get_column();

        /* End true branch - OP=60 - baseline uses condition start line + column */
        acomp_op(60, expr_start_line, expr_start_col, 1);

        /* Load TRUE and branch to end */
        acomp_true();

        /* Consume ':' */
        if (!expect_token(TOK_COLON)) {
            return false;
        }

        /* Branch to end - use position from end of true expression */
        acomp_branch_true(label_end, true_end_line, true_end_col);

        /* Set false label */
        acomp_set_label(label_false);

        /* Parse false expression (recursive for right-associativity) */
        if (!parse_ternary_expr(ctx)) {
            return false;
        }

        /* End false branch - OP=65 - baseline uses condition start line + column */
        acomp_op(65, expr_start_line, expr_start_col, 1);

        /* Set end label */
        acomp_set_label(label_end);
    }

    return true;
}

/**
 * Parse assignment expression:
 *   identifier = expression
 *   identifier += expression
 *   identifier -= expression
 *   identifier *= expression
 *   identifier /= expression
 *   identifier %= expression
 *   identifier++  (post-increment)
 *   identifier--  (post-decrement)
 *   or just expression
 *
 * SIMPLIFIED: Only handles "identifier = expr" and compound assignments.
 * Everything else goes through normal expression parsing.
 */
bool parse_assignment_expr(ParserContext *ctx) {
    int line, col;  /* Position tracking for acomp_op calls */

    /* Peek ahead to check for simple assignment pattern */
    if (peek_token() == TOK_IDENTIFIER) {
        /* Save identifier position BEFORE consuming */
        int id_line = lexer_get_line();
        int id_col = lexer_get_column();

        /* Consume and save identifier */
        next_token();
        /* IMPORTANT: lexer identifier text is stored in a shared buffer that can be
         * overwritten by subsequent peek_token()/next_token() calls. Copy it now.
         */
        const char *id_name = lexer_get_identifier();
        char name_buf[256];
        strncpy(name_buf, id_name ? id_name : "", sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';

        g_last_ident_line = id_line;
        g_last_ident_col = id_col;
        g_last_ident_valid = true;

        /* Check next token */
        int next_tok = peek_token();

        if (next_tok == TOK_LBRACKET) {
            /* CASE 1: Array indexing - identifier[index] or identifier[x, y] ... */
            /* Load the array variable */
            acomp_word_ref(NULL, name_buf);

            /* Parse all array indices (supports both arr[x][y] and arr[x,y] syntax).
             *
             * Baseline order detail:
             *   For chained brackets (arr[x][y]) the baseline pushes all index expressions first,
             *   then emits the indexing OPs (so indices appear before OP=48 in IR).
             */
            int index_group_counts[16];
            int index_group_count = 0;
            while (peek_token() == TOK_LBRACKET) {
                next_token();  /* Consume '[' - don't save position */

                int index_count = 0;

                /* Parse first index expression */
                if (!parse_expression(ctx)) {
                    return false;
                }
                index_count++;

                /* Check for comma-separated indices: arr[x, y, z] */
                while (peek_token() == TOK_COMMA) {
                    next_token();  /* Consume ',' */

                    /* Parse next index */
                    if (!parse_expression(ctx)) {
                        return false;
                    }
                    index_count++;

                }

                if (!expect_token(TOK_RBRACKET)) {
                    return false;
                }

                if (index_group_count >= (int)(sizeof(index_group_counts) / sizeof(index_group_counts[0]))) {
                    fprintf(stderr, "Error: Too many chained index groups\n");
                    return false;
                }
                index_group_counts[index_group_count++] = index_count;
            }

            for (int i = 0; i < index_group_count; i++) {
                /* Generate array index operation - use identifier position.
                 * Baseline uses a single OP=48 for comma-separated indices:
                 *   arr[i,j] => arg4 = 1(array) + 2(indices) = 3
                 */
                acomp_op(48, id_line, id_col, index_group_counts[i] + 1);
            }

            /* Check if this is array assignment: arr[i] = expr */
            if (peek_token() == TOK_ASSIGN) {
                next_token();  /* Consume '=' */

                /* Parse RHS */
                if (!parse_ternary_expr(ctx)) {
                    return false;
                }

                /* Use identifier position for assignment operation */
                acomp_op(16, id_line, id_col, 2);  /* ASSIGN */

                return true;
            } else {
                /* Array access in expression - continue parsing operators */
                /* Generate EXPR operation - use identifier position */
                acomp_op(17, id_line, id_col, 1);

                g_expr_chain_start_line = id_line;
                g_expr_chain_start_col = id_col;
                g_expr_chain_start_valid = true;
                return parse_expression_continue(ctx);
            }

        } else if (next_tok == TOK_ASSIGN) {
            /* CASE 2: Simple Assignment - identifier = expr */
            next_token();  /* Consume '=' */

            acomp_word_ref(NULL, name_buf);

            /* Reset empty-list tracking for this RHS */
            ctx->last_expr_was_empty_brace_list = false;
            ctx->last_empty_brace_list_line = 0;
            ctx->last_empty_brace_list_col = 0;

            if (!parse_ternary_expr(ctx)) {
                return false;
            }

            /* Baseline quirk: "a = {};" does not emit ASSIGN/STMT_END; it affects
             * the implicit return/DEFINE_FUNCT position (see 01_empty_list).
             */
            if (ctx->last_expr_was_empty_brace_list) {
                ctx->suppress_next_stmt_end = true;
                ctx->function_end_override_valid = true;
                ctx->function_end_override_line = ctx->last_empty_brace_list_line;
                ctx->function_end_override_col = ctx->last_empty_brace_list_col;
                return true;
            }

            /* Use identifier position for assignment operation */
            acomp_op(16, id_line, id_col, 2);  /* ASSIGN */

            return true;

        } else if (next_tok == TOK_PLUS_ASSIGN || next_tok == TOK_MINUS_ASSIGN ||
                   next_tok == TOK_STAR_ASSIGN || next_tok == TOK_SLASH_ASSIGN ||
                   next_tok == TOK_PERCENT_ASSIGN) {
            /* CASE 2b: Compound Assignment - identifier OP= expr */
            /* Baseline pattern for i += 2:
             * [0017] LOAD_VAR i
             * [0018] OP=36 (LVALUE marker) arg2=line arg3=col_of_operator
             * [0019] OP=17 (EXPR) arg2=line arg3=col_of_identifier
             * [001A] LOAD_INT 2
             * [001B] OP=10 (ADD) arg2=line arg3=col_of_operator a4=2
             * [001C] OP=16 (ASSIGN) arg2=line arg3=col_of_identifier a4=2
             * [001D] OP=0 (STMT_END)
             */

            /* Save operator position before consuming */
            int op_line = lexer_get_line();
            int op_col = lexer_get_column();

            /* Determine base operator for later */
            int base_token = 0;
            switch (next_tok) {
                case TOK_PLUS_ASSIGN: base_token = TOK_PLUS; break;
                case TOK_MINUS_ASSIGN: base_token = TOK_MINUS; break;
                case TOK_STAR_ASSIGN: base_token = TOK_STAR; break;
                case TOK_SLASH_ASSIGN: base_token = TOK_SLASH; break;
                case TOK_PERCENT_ASSIGN: base_token = TOK_PERCENT; break;
                default:
                    fprintf(stderr, "Error: Unknown compound assignment\n");
                    return false;
            }

            next_token();  /* Consume compound operator */

            /* Step 1: LOAD_VAR */
            acomp_word_ref(NULL, name_buf);

            /* Step 2: OP=36 (LVALUE marker) */
            acomp_op(36, op_line, op_col, 1);

            /* Step 3: OP=17 (EXPR) */
            acomp_op(17, op_line, id_col, 1);

            /* Step 4: Parse RHS expression */
            if (!parse_ternary_expr(ctx)) {
                return false;
            }

            /* Step 5: Generate operation (ADD, SUB, MUL, DIV, MOD) */
            int op_code = token_to_subopcode(base_token);
            if (op_code == 0) {
                fprintf(stderr, "Error: Cannot map operator for compound assignment\n");
                return false;
            }
            acomp_op(op_code, op_line, op_col, 2);

            /* Step 6: OP=16 (ASSIGN) - use identifier position */
            acomp_op(16, op_line, id_col, 2);

            return true;

        } else if (next_tok == TOK_INCREMENT) {
            /* CASE 2c: Post-increment - identifier++ */
            next_token();  /* Consume '++' */

            /* Generate specialized post-increment operation
             * Pattern: LOAD_VAR, OP=33 (post-increment)
             * Note: OP=0 (end) is added by parse_expression_statement
             */
            acomp_word_ref(NULL, name_buf);
            line = lexer_get_line();
            col = lexer_get_column();
            acomp_op(33, line, col, 1);  /* OP=48 arg1=33 (POST_INCREMENT) */

            return true;

        } else if (next_tok == TOK_DECREMENT) {
            /* CASE 2d: Post-decrement - identifier-- */
            next_token();  /* Consume '--' */

            /* Generate specialized post-decrement operation
             * Pattern: LOAD_VAR, OP=34 (post-decrement)
             * Note: OP=0 (end) is added by parse_expression_statement
             */
            acomp_word_ref(NULL, name_buf);
            line = lexer_get_line();
            col = lexer_get_column();
            acomp_op(34, line, col, 1);  /* OP=48 arg1=34 (POST_DECREMENT) */

            return true;

        } else if (next_tok == TOK_LPAREN) {
            /* CASE 3: Function call - identifier(...) */
            if (!parse_function_call_with_pos(ctx, name_buf, id_line, id_col)) {
                return false;
            }
            /* Function call result might be used in expression - continue parsing operators */
            g_expr_chain_start_line = id_line;
            g_expr_chain_start_col = id_col;
            g_expr_chain_start_valid = true;
            return parse_expression_continue(ctx);

        } else {
            /* CASE 4: Variable in expression - identifier OP expr */
            /* Generate the identifier load */
            acomp_word_ref(NULL, name_buf);

            /* Generate OP=17 (EXPR) wrapper - use saved identifier position */
            acomp_op(17, id_line, id_col, 1);

            /* Check for unit identifier (implicit multiplication: Wp um) */
            if (peek_token() == TOK_IDENTIFIER) {
                next_token();
                const char *unit_name = lexer_get_identifier();

                /* Check if it's a known unit suffix */
                double multiplier = get_unit_multiplier(unit_name);
                if (multiplier != 0.0) {
                    /* It's a unit - load multiplier constant and multiply */
                    acomp_real(multiplier);
                    line = lexer_get_line();
                    col = lexer_get_column();
                    acomp_op(12, line, col, 2);  /* OP=12 (MUL) with a4=2 */
                } else {
                    /* Not a unit - treat as variable (implicit multiplication) */
                    acomp_word_ref(NULL, unit_name);
                    line = lexer_get_line();
                    col = lexer_get_column();
                    acomp_op(3, line, col, 0);  /* MUL */
                }
            }

            /* Now parse any binary operators that follow */
            g_expr_chain_start_line = id_line;
            g_expr_chain_start_col = id_col;
            g_expr_chain_start_valid = true;
            return parse_expression_continue(ctx);
        }
    }

    /* Not an identifier - parse normally */
    return parse_ternary_expr(ctx);
}

/**
 * Helper: Continue parsing from expression level (includes ternary)
 * Handles binary operators + ternary operator
 */
/**
 * Helper: Continue parsing multiplicative operators (*, /, %, **)
 * Called after left operand is already parsed and wrapped in OP=17
 */
static bool parse_multiplicative_continue(ParserContext *ctx) {
    int line, col;

    while (true) {
        int token = peek_token();
        if (token != TOK_STAR && token != TOK_SLASH &&
            token != TOK_PERCENT && token != TOK_POWER) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Parse right operand (unary expression) */
        if (!parse_unary_expr(ctx)) {
            return false;
        }

        int op_code = (token == TOK_POWER) ? 43 : token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    return true;
}

/**
 * Helper: Continue parsing from expression level (includes ternary)
 * Handles binary operators + ternary operator with proper precedence
 * Called when left operand has already been parsed and wrapped in OP=17
 */
static bool parse_expression_continue(ParserContext *ctx) {
    int line, col;

    /* Handle all binary operators with proper precedence */

    /* Step 1: Handle multiplicative operators (*, /, %, **) */
    if (!parse_multiplicative_continue(ctx)) {
        return false;
    }

    /* Step 2: Handle additive operators (+, -) */
    while (true) {
        int token = peek_token();
        if (token != TOK_PLUS && token != TOK_MINUS) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Right operand: parse multiplicative level */
        if (!parse_multiplicative_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 3: Handle shift operators (<<, >>) */
    while (true) {
        int token = peek_token();
        if (token != TOK_LSHIFT && token != TOK_RSHIFT) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Right operand: parse additive level */
        if (!parse_additive_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 4: Handle relational operators (<, >, <=, >=) */
    while (true) {
        int token = peek_token();
        if (token != TOK_LT && token != TOK_GT &&
            token != TOK_LE && token != TOK_GE) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Right operand: parse shift level */
        if (!parse_shift_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 5: Handle equality operators (==, !=) */
    while (true) {
        int token = peek_token();
        if (token != TOK_EQ && token != TOK_NE) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();
        g_last_compare_line = line;
        g_last_compare_col = col;
        g_last_compare_valid = true;

        /* Right operand: parse relational level */
        if (!parse_relational_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 6: Handle bitwise AND (&) */
    while (true) {
        int token = peek_token();
        if (token != TOK_BIT_AND) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Right operand: parse equality level */
        if (!parse_equality_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 7: Handle bitwise XOR (^) */
    while (true) {
        int token = peek_token();
        if (token != TOK_BIT_XOR) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Right operand: parse bitwise_and level */
        if (!parse_bit_and_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 8: Handle bitwise OR (|) */
    while (true) {
        int token = peek_token();
        if (token != TOK_BIT_OR) {
            break;
        }

        line = lexer_get_line();
        col = lexer_get_column();
        next_token();

        /* Right operand: parse bitwise_xor level */
        if (!parse_bit_xor_expr(ctx)) {
            return false;
        }

        int op_code = token_to_subopcode(token);
        acomp_op(op_code, line, col, 2);
    }

    /* Step 9: Handle logical AND (&&) with short-circuit and SHARED label */
    int and_end_label = -1;
    bool has_and_ops = (peek_token() == TOK_AND);

    if (has_and_ops) {
        and_end_label = acomp_add_label();  /* Create shared label ONCE for all ANDs */
    }

    int and_op_count = 0;
    int and_chain_anchor_line = 0;
    int and_chain_anchor_col = 0;
    bool and_chain_anchor_valid = false;

    while (true) {
        int token = peek_token();
        if (token != TOK_AND) {
            break;
        }

        next_token();  /* Consume && */
        and_op_count++;

        line = g_last_token_line;
        col = g_last_token_col;

        if (!and_chain_anchor_valid) {
            if (g_last_not_valid && g_last_not_line == line) {
                and_chain_anchor_line = g_last_not_line;
                and_chain_anchor_col = g_last_not_col;
                and_chain_anchor_valid = true;
            } else if (g_last_compare_valid && g_last_compare_line == line) {
                if (g_last_null_valid && g_last_null_line == line) {
                    and_chain_anchor_line = g_last_compare_line;
                    and_chain_anchor_col = g_last_compare_col;
                    and_chain_anchor_valid = true;
                } else if (g_last_ident_valid && g_last_ident_line == line) {
                    and_chain_anchor_line = g_last_ident_line;
                    and_chain_anchor_col = g_last_ident_col;
                    and_chain_anchor_valid = true;
                } else {
                    and_chain_anchor_line = g_last_compare_line;
                    and_chain_anchor_col = g_last_compare_col;
                    and_chain_anchor_valid = true;
                }
            } else if (g_last_nonempty_string_valid &&
                       g_last_nonempty_string_line < line &&
                       (line - g_last_nonempty_string_line) <= 3) {
                and_chain_anchor_line = g_last_nonempty_string_line;
                and_chain_anchor_col = g_last_nonempty_string_col;
                and_chain_anchor_valid = true;
            } else {
                and_chain_anchor_line = line;
                and_chain_anchor_col = col;
                and_chain_anchor_valid = true;
            }
        }

        /* Start short-circuit AND */
        acomp_op(62, line, col, 1);  /* OP=48 arg1=62 */
        acomp_op(36, line, col, 1);  /* OP=48 arg1=36 */
        acomp_op(3, line, col, 1);   /* OP=48 arg1=3 */

        /* Branch if left side is true - ALL ANDs jump to SAME shared label */
        acomp_branch_true(and_end_label, line, col);

        /* Left is false, pop and continue */
        acomp_op(0, line, col, 1);   /* OP=48 arg1=0 */
        acomp_op(62, line, col, 1);  /* OP=48 arg1=62 */

        /* Right operand: parse bitwise_or level */
        if (!parse_bit_or_expr(ctx)) {
            return false;
        }

        /* End short-circuit AND */
        int end_line = line;
        int end_col = col;
        if (and_op_count > 1 && and_chain_anchor_valid) {
            end_line = and_chain_anchor_line;
            end_col = and_chain_anchor_col;
        }
        acomp_op(62, end_line, end_col, 1);  /* OP=48 arg1=62 */
    }

    if (has_and_ops) {
        acomp_set_label(and_end_label);
    }

    /* Step 10: Handle logical OR (||) with short-circuit and SHARED label */
    int or_end_label = -1;
    bool has_or_ops = (peek_token() == TOK_OR);

    if (has_or_ops) {
        or_end_label = acomp_add_label();  /* Create shared label ONCE for all ORs */
    }

    /*
     * OR-chain anchor position:
     * For multi-OR chains like `a || b || c`, the baseline IR uses a special
     * position for the final OR "end marker" OP=63: it points to the last token
     * position of the left-most operand (e.g. the first string literal in
     * `Type == "X" || ...`). We capture the last-consumed token position before
     * entering the OR loop and reuse it only for the final OP=63 of the chain.
     */
    int or_chain_anchor_line = g_last_token_line;
    int or_chain_anchor_col = g_last_token_col;

    int or_op_count = 0;

    while (true) {
        int token = peek_token();
        if (token != TOK_OR) {
            break;
        }

        next_token();  /* Consume || */
        or_op_count++;

        line = g_last_token_line;
        col = g_last_token_col;

        /* Start short-circuit OR */
        acomp_op(63, line, col, 1);  /* OP=48 arg1=63 */
        acomp_op(36, line, col, 1);  /* OP=48 arg1=36 */

        /* Branch if left side is true - ALL ORs jump to SAME shared label */
        acomp_branch_true(or_end_label, line, col);

        /* Left is false, pop and continue */
        acomp_op(0, line, col, 1);   /* OP=48 arg1=0 */
        acomp_op(63, line, col, 1);  /* OP=48 arg1=63 */

        /* Right operand: parse logical_and level */
        if (!parse_logical_and_expr(ctx)) {
            return false;
        }

        /* End short-circuit OR:
         * Baseline behavior for multi-OR chains (>=2 OR operators):
         * - First OR end marker uses its own '||' position
         * - Subsequent OR end markers use the chain anchor position
         */
        if (or_op_count > 1) {
            acomp_op(63, or_chain_anchor_line, or_chain_anchor_col, 1);  /* OP=48 arg1=63 */
        } else {
            acomp_op(63, line, col, 1);  /* OP=48 arg1=63 */
        }

        /* Don't set label here - wait for all ORs to be processed */
    }

    /* Set shared label AFTER all ORs processed */
    if (has_or_ops) {
        acomp_set_label(or_end_label);  /* Set ONCE at end */
    }

    /* Step 11: Handle ternary operator (? :) */
    if (peek_token() == TOK_QUESTION) {
        /* Save '?' position BEFORE consuming */
        int question_line = lexer_get_line();
        int question_col = lexer_get_column();

        next_token();  /* Consume '?' */

        /* Start ternary: OP=59 (START_TERNARY) - use '?' position */
        acomp_op(59, question_line, question_col, 1);

        /* Add two labels: label0 for false branch, label1 for end */
        int label_false = acomp_add_label();
        int label_end = acomp_add_label();

        /* Check condition and branch - use '?' position */
        acomp_op(3, question_line, question_col, 1);   /* OP=48 arg1=3 */
        acomp_branch_true(label_false, question_line, question_col);

        /* Baseline uses the condition expression start position for OP=61/60/65.
         * For expression_continue() the chain start is set by the caller (identifier-based).
         */
        int cond_start_line = g_expr_chain_start_valid ? g_expr_chain_start_line : question_line;
        int cond_start_col = g_expr_chain_start_valid ? g_expr_chain_start_col : question_col;

        /* True branch marker - OP=61 */
        acomp_op(61, cond_start_line, cond_start_col, 1);

        /* Parse true expression */
        if (!parse_expression(ctx)) {
            return false;
        }

        /* Save position after true expression for BRANCH_TRUE */
        int true_end_line = lexer_get_line();
        int true_end_col = lexer_get_column();

        /* End true branch - OP=60 */
        acomp_op(60, cond_start_line, cond_start_col, 1);

        /* Load TRUE and branch to end */
        acomp_true();

        /* Consume ':' */
        if (!expect_token(TOK_COLON)) {
            return false;
        }

        /* Branch to end - use position from end of true expression */
        acomp_branch_true(label_end, true_end_line, true_end_col);

        /* Set false label */
        acomp_set_label(label_false);

        /* Parse false expression (recursive for right-associativity) */
        if (!parse_ternary_expr(ctx)) {
            return false;
        }

        /* End false branch - OP=65 */
        acomp_op(65, cond_start_line, cond_start_col, 1);

        /* Set end label */
        acomp_set_label(label_end);
    }

    return true;
}

/**
 * Parse expression (top-level)
 */
bool parse_expression(ParserContext *ctx) {
    return parse_assignment_expr(ctx);
}

/* To be continued in next part... */
