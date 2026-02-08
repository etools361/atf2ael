/*
 * ael_parser_functions.c
 * Function Definition Parsing and Main Entry Point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ael_parser_new.h"
#include "ir_generator.h"
#include "lexer_state.h"

/* External functions */
extern int next_token(void);
extern int peek_token(void);
extern bool expect_token(int expected_token);
extern void parser_error(const char *message);
extern bool parse_statement(ParserContext *ctx);
extern bool parse_expression_statement(ParserContext *ctx);
extern bool parse_block_statement(ParserContext *ctx);

/* External globals */
extern int AcompDepth;
extern ParserContext g_parser_ctx;

/* ============================================================================
 * Function Definition Parsing
 * ============================================================================ */

/**
 * Parse function parameter list:
 *   ( param1, param2, ... )
 */
#define MAX_PARAMS 32
static bool parse_parameter_list(ParserContext *ctx, int *param_count, char param_names[][256]) {
    *param_count = 0;

    /* Expect '(' */
    if (!expect_token(TOK_LPAREN)) {
        return false;
    }

    /* Empty parameter list */
    if (peek_token() == TOK_RPAREN) {
        next_token();  /* Consume ')' */
        return true;
    }

    /* Parse parameters */
    while (true) {
        /* Expect parameter name */
        if (peek_token() != TOK_IDENTIFIER) {
            parser_error("expected parameter name");
            return false;
        }

        next_token();  /* Consume parameter name */
        const char *param_name = lexer_get_identifier();  /* Get actual name */

        /* Save parameter name for later */
        if (*param_count >= MAX_PARAMS) {
            parser_error("too many parameters");
            return false;
        }
        strncpy(param_names[*param_count], param_name, 255);
        param_names[*param_count][255] = '\0';
        (*param_count)++;

        /* Check for more parameters */
        if (peek_token() == TOK_COMMA) {
            next_token();  /* Consume ',' */
            continue;
        } else {
            break;
        }
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    return true;
}

/**
 * Parse function definition:
 *   defun name ( params ) { body }
 */
bool parse_function_definition(ParserContext *ctx, int defun_line) {
    /* Already consumed 'defun' keyword */

    /* Expect function name */
    if (peek_token() != TOK_IDENTIFIER) {
        parser_error("expected function name after 'defun'");
        return false;
    }

    next_token();  /* Consume function name */
    const char *func_name_temp = lexer_get_identifier();  /* Get actual name */

    /* Save function name before it gets overwritten */
    char func_name_buf[256];
    strncpy(func_name_buf, func_name_temp, sizeof(func_name_buf) - 1);
    func_name_buf[sizeof(func_name_buf) - 1] = '\0';

    /* Parse parameter list */
    int param_count = 0;
    char param_names[MAX_PARAMS][256];
    if (!parse_parameter_list(ctx, &param_count, param_names)) {
        return false;
    }

    /* Begin function - pass defun source line number */
    acomp_begin_funct(NULL, func_name_buf, param_count, 0, defun_line);

    /* Now add parameters in the correct order */
    for (int i = 0; i < param_count; i++) {
        acomp_add_arg(param_names[i]);
    }
    int func_start_addr = ir_get_count();  /* Get address after BEGIN_FUNCT */

    /* Set function context */
    ctx->in_function = true;
    ctx->function_depth = 1;
    ctx->local_var_count = param_count;  /* Parameters count as locals */
    ctx->function_end_override_valid = false;
    AcompDepth = 1;

    /* Save IR generator's local count before function body (should equal param_count) */
    int saved_local_count = ir_get_local_count();

    /* Declare local variables count (will be updated) */
    int num_local_offset = ir_get_count();
    acomp_num_local();

    /* Parse function body */
    if (!parse_block_statement(ctx)) {
        return false;
    }

    /* Update local count */
    /* TODO: Patch the NUM_LOCAL instruction with actual count */

    /* Add final NUM_LOCAL */
    acomp_num_local();

    /* Add DROP_LOCAL only if there are function-level local variables beyond parameters */
    /* Use IR generator's local count, not ctx->local_var_count (which includes block-scoped vars) */
    if (ir_get_local_count() > saved_local_count) {
        acomp_drop_local(saved_local_count);
    }

    /* Add implicit return NULL if no explicit return */
    acomp_null();

    int line = lexer_get_line();
    int col = lexer_get_column();
    if (ctx->function_end_override_valid) {
        line = ctx->function_end_override_line;
        col = ctx->function_end_override_col;
    }
    acomp_op(20, line, col, 1);  /* RETURN */

    /* Define function (close function definition) */
    /* Use function end line number (where '}' is) */
    acomp_define_funct(line, ctx->function_end_override_valid ? col : 0);
    ctx->function_end_override_valid = false;

    /* Reset function context */
    ctx->in_function = false;
    ctx->function_depth = 0;
    ctx->local_var_count = 0;
    AcompDepth = 0;

    return true;
}

/* ============================================================================
 * Top-Level Parsing
 * ============================================================================ */

/**
 * Parse global statement (top-level)
 */
bool parse_global_statement(ParserContext *ctx) {
    int token = peek_token();

    switch (token) {
        case TOK_DEFUN: {
            /* Capture line number BEFORE consuming defun */
            int defun_line = lexer_get_line();
            next_token();  /* Consume 'defun' */
            return parse_function_definition(ctx, defun_line);
        }

        case TOK_DECL:
            next_token();  /* Consume 'decl' */
            return parse_decl_statement(ctx);

        case TOK_LBRACE:
            /* Global block (like in save_project_state.ael) */
            AcompDepth = 1;  /* Treat as function-like scope */

            /* Start with NUM_LOCAL */
            acomp_num_local();

            /* Parse block */
            bool result = parse_block_statement(ctx);

            if (result) {
                /* End with NUM_LOCAL and DROP_LOCAL */
                acomp_num_local();
                acomp_drop_local(0);
            }

            AcompDepth = 0;
            return result;

        case TOK_IF:
        case TOK_WHILE:
        case TOK_FOR:
        case TOK_DO:
        case TOK_SWITCH:
            /* Control flow statements at global scope */
            return parse_statement(ctx);

        case TOK_EOF:
            /* End of file */
            return true;

        default:
            /* Allow expression statements at global scope (e.g., function calls) */
            return parse_expression_statement(ctx);
    }
}

/**
 * Main entry point: Parse AEL program
 */
bool parse_ael_program(void) {
    /* Initialize parser context */
    memset(&g_parser_ctx, 0, sizeof(g_parser_ctx));
    g_parser_ctx.in_function = false;
    g_parser_ctx.function_depth = 0;
    g_parser_ctx.local_var_count = 0;
    g_parser_ctx.had_error = false;
    g_parser_ctx.error_count = 0;

    /* Initialize IR generator */
    ir_init();
    AcompDepth = 0;

    /* Parse global statements */
    while (peek_token() != TOK_EOF) {
        if (!parse_global_statement(&g_parser_ctx)) {
            /* Error occurred, but continue parsing */
            if (g_parser_ctx.error_count > 20) {
                fprintf(stderr, "Too many errors, stopping parse\n");
                break;
            }
        }
    }

    /* Return success if no errors */
    return !g_parser_ctx.had_error;
}
