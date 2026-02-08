/*
 * ael_parser_statements.c
 * Statement and Function Parsing
 *
 * This file contains:
 * - Statement parsing (if, while, for, return, etc.)
 * - Function definition parsing
 * - Function call parsing
 * - Declaration parsing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ael_parser_new.h"
#include "ir_generator.h"
#include "lexer_state.h"

/* External functions from ael_parser_new.c */
extern int next_token(void);
extern int peek_token(void);
extern bool expect_token(int expected_token);
extern void parser_error(const char *message);

/* External IR generation functions */
extern int AcompDepth;

/* ============================================================================
 * Function Call Parsing
 * ============================================================================ */

/**
 * Parse function call with position:
 *   identifier ( arg1, arg2, ... )
 */
bool parse_function_call_with_pos(ParserContext *ctx, const char *func_name, int func_line, int func_col) {
    /* Load function name */
    acomp_word_ref(NULL, func_name);

    /* Save position of '(' before consuming */
    int lparen_line = lexer_get_line();
    int lparen_col = lexer_get_column();

    /* Consume '(' (already peeked by caller) */
    next_token();

    /* Parse arguments */
    int arg_count = 0;
    if (!parse_argument_list(ctx, &arg_count)) {
        return false;
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    /* Generate PUSH_ARGS - use '(' position, a4=arg_count */
    acomp_op(56, lparen_line, lparen_col, arg_count);

    /* Generate CALL and EXPR - use function name position */
    acomp_op(48, func_line, func_col, 2);  /* CALL a4=2 */
    acomp_op(17, func_line, func_col, 1);  /* EXPR a4=1 */

    return true;
}

/**
 * Parse function call (legacy version without position):
 *   identifier ( arg1, arg2, ... )
 */
bool parse_function_call(ParserContext *ctx, const char *func_name) {
    /* Load function name */
    acomp_word_ref(NULL, func_name);

    /* Save position of '(' before consuming */
    int line = lexer_get_line();
    int col = lexer_get_column();

    /* Consume '(' (already peeked by caller) */
    next_token();

    /* Parse arguments */
    int arg_count = 0;
    if (!parse_argument_list(ctx, &arg_count)) {
        return false;
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    /* Generate PUSH_ARGS - a4 should be arg_count */
    acomp_op(56, line, col, arg_count);  /* OP=48 arg1=56 arg2=line arg3=col a4=arg_count */

    /* Generate CALL and EXPR */
    acomp_op(48, line, col, 2);  /* OP=48 arg1=48 (CALL) a4=2 */
    acomp_op(17, line, col, 1);  /* OP=48 arg1=17 (EXPR) a4=1 */

    return true;
}

/**
 * Parse argument list for function call
 */
bool parse_argument_list(ParserContext *ctx, int *arg_count) {
    *arg_count = 0;

    /* Empty argument list */
    if (peek_token() == TOK_RPAREN) {
        return true;
    }

    /* Parse first argument */
    if (!parse_expression(ctx)) {
        return false;
    }
    (*arg_count)++;

    /* Parse remaining arguments */
    while (peek_token() == TOK_COMMA) {
        next_token();  /* Consume ',' */

        /* Check for trailing comma */
        if (peek_token() == TOK_RPAREN) {
            /* Trailing comma before closing paren:
             * Baseline compiler treats this as an extra NULL argument.
             */
            acomp_null();
            (*arg_count)++;
            return true;
        }

        if (!parse_expression(ctx)) {
            return false;
        }
        (*arg_count)++;
    }

    return true;
}

/* ============================================================================
 * Declaration Parsing
 * ============================================================================ */

/**
 * Parse declaration statement:
 *   decl var1, var2, var3;
 *   decl var1 = expr1, var2 = expr2;
 */
bool parse_decl_statement(ParserContext *ctx) {
    /* Already consumed 'decl' keyword */

    /* Parse variable list */
    while (true) {
        /* Expect identifier */
        if (peek_token() != TOK_IDENTIFIER) {
            parser_error("expected identifier after 'decl'");
            return false;
        }

        next_token();  /* Consume identifier */
        const char *var_name = lexer_get_identifier();

        /* Add variable - use AcompDepth to determine scope */
        if (AcompDepth > 0) {
            /* Inside a block or function - use local */
            acomp_add_local(var_name);
            ctx->local_var_count++;
        } else {
            /* Global scope */
            acomp_add_global(NULL, var_name);
        }

        /* Check for initialization */
        if (peek_token() == TOK_ASSIGN) {
            /* Save '=' position before consuming */
            int line = lexer_get_line();
            int col = lexer_get_column();

            next_token();  /* Consume '=' */

            /* Load variable reference */
            acomp_word_ref(NULL, var_name);

            /* Parse initializer expression */
            if (!parse_expression(ctx)) {
                return false;
            }

            /* Generate assignment (use '=' position) */
            acomp_op(16, line, col, 2);  /* ASSIGN */
            acomp_op(0, line, col, 1);   /* STMT_END */
        }

        /* Check for more variables */
        if (peek_token() == TOK_COMMA) {
            next_token();  /* Consume ',' */
            continue;
        } else {
            break;
        }
    }

    /* Expect ';' */
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    return true;
}

/* ============================================================================
 * Statement Parsing
 * ============================================================================ */

/**
 * Parse block statement:
 *   { statement* }
 */
bool parse_block_statement(ParserContext *ctx) {
    /* Expect '{' */
    if (!expect_token(TOK_LBRACE)) {
        return false;
    }

    bool had_any_statement = false;

    /* Parse statements until '}' */
    while (peek_token() != TOK_RBRACE && peek_token() != TOK_EOF) {
        if (!parse_statement(ctx)) {
            return false;
        }
        had_any_statement = true;
    }

    ctx->last_block_had_statements = had_any_statement;

    /* Save position of '}' before consuming it
     * CRITICAL: Must do this IMMEDIATELY before expect_token()
     * because any intervening peek/next call will update g_current_line
     */
    ctx->last_rbrace_line = lexer_get_line();
    ctx->last_rbrace_col = lexer_get_column();

    /* Expect '}' */
    if (!expect_token(TOK_RBRACE)) {
        return false;
    }

    return true;
}

/**
 * Parse if statement (internal version with optional shared end label and position):
 *   if ( expr ) statement
 *   if ( expr ) statement else statement
 *
 * @param ctx Parser context
 * @param shared_end_label Optional label to use as the final exit point (for elseif chains)
 *                         If -1, create a new end label
 * @param shared_jump_line Line number to use for BRANCH_TRUE in elseif chains
 * @param shared_jump_col Column number to use for BRANCH_TRUE in elseif chains
 */
static bool parse_if_statement_internal(ParserContext *ctx, int shared_end_label,
                                       int shared_jump_line, int shared_jump_col) {
    /* Already consumed 'if' keyword */

    /* Expect '(' */
    if (!expect_token(TOK_LPAREN)) {
        return false;
    }

    /* Parse condition */
    if (!parse_expression(ctx)) {
        return false;
    }

    /* Expect ')' and save position after it */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    /* Save position after ')' for OP=59, OP=3, and BRANCH_TRUE */
    int line = lexer_get_line();
    int col = lexer_get_column();

    /* Generate OP=59 (conditional jump preparation) */
    acomp_op(59, line, col, 1);

    /* Create label for else block (or end if no else) */
    int else_label = acomp_add_label();

    /* Generate OP=3 (some operation) */
    acomp_op(3, line, col, 1);

    /* Generate BRANCH_TRUE - if condition is false, jump to else_label */
    acomp_branch_true(else_label, line, col);

    /* Check if then body is a block (which will handle its own NUM_LOCAL) */
    bool then_is_block = (peek_token() == TOK_LBRACE);

    /* Save IR generator's local count before entering then block (excludes parameters) */
    int then_entry_local_count = ir_get_local_count();

    /* Increment depth for then block only if it's a real block */
    if (then_is_block) {
        AcompDepth++;
        /* Generate NUM_LOCAL (entering then block) */
        acomp_num_local();
    }

    /* Parse then block statement */
    bool saved_block_scope_managed_by_parent = ctx->block_scope_managed_by_parent;
    if (then_is_block) {
        ctx->block_scope_managed_by_parent = true;
    }
    if (!parse_statement(ctx)) {
        ctx->block_scope_managed_by_parent = saved_block_scope_managed_by_parent;
        return false;
    }
    ctx->block_scope_managed_by_parent = saved_block_scope_managed_by_parent;

    /* Capture position after then statement completes */
    int then_end_line, then_end_col;
    if (then_is_block) {
        /* Use saved position of '}' from parse_block_statement */
        then_end_line = ctx->last_rbrace_line;
        then_end_col = ctx->last_rbrace_col;
    } else {
        /* For non-block statements, use the saved statement end position */
        then_end_line = ctx->last_stmt_end_line;
        then_end_col = ctx->last_stmt_end_col;
    }

    /* Generate NUM_LOCAL and decrement depth only if it's a real block */
    if (then_is_block) {
        /* Baseline quirk: empty blocks emit only the entry NUM_LOCAL. */
        if (ctx->last_block_had_statements) {
            /* Generate NUM_LOCAL (exiting then block) */
            acomp_num_local();
        }
        /* If local variables were declared in the block, drop them */
        if (ir_get_local_count() > then_entry_local_count) {
            acomp_drop_local(then_entry_local_count);
        }
        /* Decrement depth back to original level */
        AcompDepth--;
    }

    /* Check for else branch */
    if (peek_token() == TOK_ELSE) {
        int else_line = lexer_get_line();
        int else_col = lexer_get_column();
        next_token();  /* Consume 'else' */

        /* Determine the end label to use */
        int end_label;
        int jump_line, jump_col;

        /* Check if this is 'else if' (elseif chain) - peek ahead */
        bool is_elseif_chain = (peek_token() == TOK_IF);

        if (shared_end_label != -1) {
            /* Use shared end label from outer if */
            end_label = shared_end_label;
            jump_line = shared_jump_line;
            jump_col = shared_jump_col;
        } else {
            /* Create new end label */
            end_label = acomp_add_label();

            if (then_is_block) {
                 /* Block statement: use closing brace position */
                 jump_line = then_end_line;
                 jump_col = then_end_col;
              } else {
                  /* Non-block statement: baseline uses special jump positions for return */
                  if (ctx->last_stmt_was_return) {
                      if (ctx->last_return_has_expr) {
                          if ((is_elseif_chain &&
                               (ctx->last_return_expr_first_token == TOK_IDENTIFIER ||
                                ctx->last_return_expr_first_token == TOK_STRING)) ||
                              (!is_elseif_chain && ctx->last_return_expr_first_token == TOK_STRING)) {
                              /* Baseline patterns:
                               * - else-if chain: return <id|string> => use returned expression start
                               * - simple if-else: return <string> => use returned expression start
                               */
                              jump_line = ctx->last_return_expr_line;
                              jump_col = ctx->last_return_expr_col;
                          } else {
                              /* Otherwise, use sentinel (1, 0) */
                              jump_line = 1;
                              jump_col = 0;
                          }
                      } else {
                          /* return; => sentinel (1, 0) */
                          jump_line = 1;
                          jump_col = 0;
                      }
                  } else {
                      /* Other statements: use actual then_end position */
                      jump_line = then_end_line;
                      jump_col = then_end_col;
                  }
             }
         }

        /* Generate unconditional jump to end (skip else block) */
        acomp_true();  /* Push TRUE on stack */
        acomp_branch_true(end_label, jump_line, jump_col);

        /* Set else_label (else block starts here) */
        acomp_set_label(else_label);

        /* Check if this is 'else if' (elseif chain) */
        if (is_elseif_chain) {
            /* This is 'else if' - capture position of 'if' keyword for elseif BRANCH_TRUE */
            int elseif_line = lexer_get_line();
            next_token();  /* Consume 'if' */

            /* Recursively parse, sharing the same end_label
             * IMPORTANT: Use elseif_line for arg2, else_col for arg3
             * - arg2 = elseif line (line of 'if' in 'else if')
             * - arg3 = else keyword column (position we're jumping from) */
            if (!parse_if_statement_internal(ctx, end_label, elseif_line, else_col)) {
                return false;
            }
        } else {
            /* Regular else block */
            /* Check if else body is a block (which will handle its own NUM_LOCAL) */
            bool else_is_block = (peek_token() == TOK_LBRACE);

            /* Save IR generator's local count before entering else block (excludes parameters) */
            int else_entry_local_count = ir_get_local_count();

            /* Increment depth for else block only if it's a real block */
            if (else_is_block) {
                AcompDepth++;
                /* Generate NUM_LOCAL (entering else block) */
                acomp_num_local();
            }

            /* Parse else block statement */
            bool saved_else_block_scope_managed_by_parent = ctx->block_scope_managed_by_parent;
            if (else_is_block) {
                ctx->block_scope_managed_by_parent = true;
            }
            if (!parse_statement(ctx)) {
                ctx->block_scope_managed_by_parent = saved_else_block_scope_managed_by_parent;
                return false;
            }
            ctx->block_scope_managed_by_parent = saved_else_block_scope_managed_by_parent;

            /* Generate NUM_LOCAL and decrement depth only if it's a real block */
            if (else_is_block) {
                /* Baseline quirk: empty blocks emit only the entry NUM_LOCAL. */
                if (ctx->last_block_had_statements) {
                    /* Generate NUM_LOCAL (exiting else block) */
                    acomp_num_local();
                }
                /* If local variables were declared in the block, drop them */
                if (ir_get_local_count() > else_entry_local_count) {
                    acomp_drop_local(else_entry_local_count);
                }
                /* Decrement depth back to original level */
                AcompDepth--;
            }
        }

        /* Set end_label only if we created it (not shared) */
        if (shared_end_label == -1) {
            acomp_set_label(end_label);
        }
    } else {
        /* No else branch - set else_label as end */
        acomp_set_label(else_label);
    }

    return true;
}

/**
 * Parse if statement (public interface):
 *   if ( expr ) statement
 *   if ( expr ) statement else statement
 */
bool parse_if_statement(ParserContext *ctx) {
    return parse_if_statement_internal(ctx, -1, -1, -1);
}

/**
 * Parse while statement:
 *   while ( expr ) statement
 */
bool parse_while_statement(ParserContext *ctx) {
    /* Already consumed 'while' keyword */

    /* Save outer loop labels (for nested loops) */
    int outer_loop_start = ctx->loop_start_label;
    int outer_loop_end = ctx->loop_end_label;
    int outer_loop_line = ctx->loop_start_line;
    int outer_loop_col = ctx->loop_start_col;

    /* Begin loop */
    acomp_begin_loop();

    /* LOOP_AGAIN marker (continue jumps here) */
    acomp_loop_again();

    /* Create label for condition check */
    int condition_label = acomp_add_label();

    /* Set condition label */
    acomp_set_label(condition_label);

    /* Expect '(' and save its position */
    if (peek_token() != TOK_LPAREN) {
        parser_error("expected '(' after 'while'");
        return false;
    }

    /* Save position of '(' token BEFORE consuming it
     * peek_token() has already read '(' and set the global position to it
     * CRITICAL: Must save position after peek but before parse_expression,
     * because parse_expression will update the global position to the last token
     */
    int lparen_line = lexer_get_line();
    int lparen_col = lexer_get_column();

    /* Consume '(' */
    next_token();

    /* Parse condition */
    if (!parse_expression(ctx)) {
        return false;
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    /* Generate OP=3 after condition - use '(' position */
    acomp_op(3, lparen_line, lparen_col, 1);

    /* LOOP_EXIT marker (break jumps here) */
    acomp_loop_exit();

    /* Create label for loop end */
    int end_label = acomp_add_label();

    /* BRANCH_TRUE to end_label - use '(' position */
    acomp_branch_true(end_label, lparen_line, lparen_col);

    /* Save loop labels for break/continue */
    ctx->loop_start_label = condition_label;
    ctx->loop_end_label = end_label;
    ctx->loop_start_line = lparen_line;
    ctx->loop_start_col = lparen_col;

    /* Save IR generator's local count before entering loop body (excludes parameters) */
    int body_entry_local_count = ir_get_local_count();

    /* Increment depth for loop body */
    AcompDepth++;

    /* Generate NUM_LOCAL (entering loop body) */
    acomp_num_local();

    /* Parse loop body */
    bool saved_while_block_scope_managed_by_parent = ctx->block_scope_managed_by_parent;
    ctx->block_scope_managed_by_parent = (peek_token() == TOK_LBRACE);
    if (!parse_statement(ctx)) {
        ctx->block_scope_managed_by_parent = saved_while_block_scope_managed_by_parent;
        return false;
    }
    ctx->block_scope_managed_by_parent = saved_while_block_scope_managed_by_parent;

    /* Generate NUM_LOCAL (exiting loop body) */
    acomp_num_local();

    /* If local variables were declared in the loop body scope, drop them (baseline emits
     * DROP_LOCAL immediately after the exit NUM_LOCAL, before the trailing LOAD_TRUE).
     */
    if (ir_get_local_count() > body_entry_local_count) {
        acomp_drop_local(body_entry_local_count);
    }

    /* Decrement depth */
    AcompDepth--;

    /* Generate unconditional jump back to condition */
    acomp_true();  /* LOAD_TRUE */
    acomp_loop_again();  /* LOOP_AGAIN marker */
    acomp_branch_true(condition_label, lparen_line, lparen_col);  /* Jump back - use '(' position */

    /* LOOP_EXIT marker */
    acomp_loop_exit();

    /* Set end label */
    acomp_set_label(end_label);

    /* End loop */
    acomp_end_loop();

    /* Restore outer loop labels (for nested loops) */
    ctx->loop_start_label = outer_loop_start;
    ctx->loop_end_label = outer_loop_end;
    ctx->loop_start_line = outer_loop_line;
    ctx->loop_start_col = outer_loop_col;

    return true;
}

/**
 * Parse do-while statement:
 *   do statement while ( expr );
 *
 * IR structure with 3 labels:
 * - label 0: loop start (body)
 * - label 1: condition check
 * - label 2: loop exit
 */
bool parse_do_while_statement(ParserContext *ctx) {
    /* Already consumed 'do' keyword */

    /* Create label 0 for loop start */
    int loop_start_label = acomp_add_label();

    /* Begin loop */
    acomp_begin_loop();

    /* Set label 0 (loop start) */
    acomp_set_label(loop_start_label);

    /* Increment depth for loop body */
    AcompDepth++;

    /* Generate NUM_LOCAL (entering loop body) */
    acomp_num_local();

    /* Parse loop body */
    bool saved_do_block_scope_managed_by_parent = ctx->block_scope_managed_by_parent;
    ctx->block_scope_managed_by_parent = (peek_token() == TOK_LBRACE);
    if (!parse_statement(ctx)) {
        ctx->block_scope_managed_by_parent = saved_do_block_scope_managed_by_parent;
        return false;
    }
    ctx->block_scope_managed_by_parent = saved_do_block_scope_managed_by_parent;

    /* Generate NUM_LOCAL (exiting loop body) */
    acomp_num_local();

    /* Decrement depth */
    AcompDepth--;

    /* LOOP_AGAIN marker (continue jumps here) */
    acomp_loop_again();

    /* Create label 1 for condition check */
    int condition_label = acomp_add_label();

    /* Set label 1 (condition check) */
    acomp_set_label(condition_label);

    /* Baseline uses the position of the 'while' keyword for the trailing BRANCH_TRUE. */
    if (peek_token() != TOK_WHILE) {
        parser_error("expected 'while' after 'do' body");
        return false;
    }
    int while_line = lexer_get_line();
    int while_col = lexer_get_column();
    next_token();  /* Consume 'while' */

    /* Expect '(' */
    if (!expect_token(TOK_LPAREN)) {
        return false;
    }

    /* Parse condition */
    if (!parse_expression(ctx)) {
        return false;
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    int line = while_line;
    int col = while_col;

    /* BRANCH_TRUE back to loop_start_label (if condition is TRUE, repeat) */
    acomp_branch_true(loop_start_label, line, col);

    /* LOOP_EXIT marker (break jumps here) */
    acomp_loop_exit();

    /* Create label 2 for loop exit */
    int exit_label = acomp_add_label();

    /* Set label 2 (loop exit) */
    acomp_set_label(exit_label);

    /* Expect ';' */
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* End loop */
    acomp_end_loop();

    return true;
}

/**
 * Parse switch statement:
 *   switch ( expr ) { case value: ... break; default: ... }
 *
 * Switch IR structure is complex with multiple labels:
 * - label 0: BRANCH_TABLE entry point
 * - label 1, 3, 4, ...: case body labels
 * - label 2: exit label (created by first case's break)
 * - label 5: default case label
 *
 * @param switch_line Line number of 'switch' keyword
 * @param switch_col Column number of 'switch' keyword
 */
bool parse_switch_statement(ParserContext *ctx, int switch_line, int switch_col) {
    /* Already consumed 'switch' keyword */

    /* Expect '(' and capture its position for BRANCH_TABLE */
    if (peek_token() != TOK_LPAREN) {
        parser_error("expected '(' after 'switch'");
        return false;
    }

    /* After peek, lexer points to '(' - capture this for later use */
    int lparen_col = lexer_get_column();

    /* Consume '(' */
    next_token();

    /* Parse switch expression */
    if (!parse_expression(ctx)) {
        return false;
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    /* Begin loop (switch uses loop structure) */
    acomp_begin_loop();

    /* Load TRUE + LOOP_AGAIN for initial setup */
    acomp_true();
    acomp_loop_again();

    /* Create and set first label (label 0) for BRANCH_TABLE entry */
    int first_label = acomp_add_label();
    acomp_branch_true(first_label, switch_line, switch_col);

    /* Create exit label but don't set it yet - it will be set at the end */
    /* The exit label will be created by the first case's break statement */
    int exit_label = -1;  /* Will be set when first break is encountered */
    bool exit_label_created = false;

    /* Save loop labels for break statements */
    int outer_loop_start = ctx->loop_start_label;
    int outer_loop_end = ctx->loop_end_label;
    int outer_loop_line = ctx->loop_start_line;
    int outer_loop_col = ctx->loop_start_col;

    /* For switch, break should jump to exit_label */
    /* We'll use a special marker to indicate we're in a switch */
    ctx->loop_start_label = first_label;
    ctx->loop_end_label = -1;  /* Will be set after first break creates the label */
    ctx->loop_start_line = switch_line;
    ctx->loop_start_col = switch_col;

    /* Expect '{' */
    if (!expect_token(TOK_LBRACE)) {
        return false;
    }

    /* Track if we've seen cases (for SET_LOOP_DEFAULT placement) */
    bool has_seen_case = false;
    bool has_default = false;

    /* Parse case and default statements */
    while (peek_token() != TOK_RBRACE && peek_token() != TOK_EOF) {
        int token = peek_token();

        if (token == TOK_CASE) {
            next_token();  /* Consume 'case' */
            has_seen_case = true;

            /* Parse case value (must be constant expression) */
            if (peek_token() == TOK_INTEGER) {
                next_token();
                int case_value = lexer_get_int();

                /* Expect ':' */
                if (!expect_token(TOK_COLON)) {
                    return false;
                }

                /* Generate ADD_CASE */
                acomp_add_case(case_value);

                /* Add and set label for this case body */
                int case_label = acomp_add_label();
                acomp_set_label(case_label);

                /* Parse case body statements until next case/default/break/rbrace */
                while (peek_token() != TOK_CASE &&
                       peek_token() != TOK_DEFAULT &&
                       peek_token() != TOK_RBRACE &&
                       peek_token() != TOK_EOF) {

                    /* Special handling for break in switch */
                    if (peek_token() == TOK_BREAK) {
                        /* Save break position */
                        int break_line = lexer_get_line();
                        int break_col = lexer_get_column();
                        next_token();  /* Consume 'break' */

                        /* Expect ';' */
                        if (!expect_token(TOK_SEMICOLON)) {
                            return false;
                        }

                        /* Generate break IR pattern */
                        acomp_true();
                        acomp_loop_exit();

                        /* Create exit label only for first break */
                        if (!exit_label_created) {
                            exit_label = acomp_add_label();
                            exit_label_created = true;
                            ctx->loop_end_label = exit_label;  /* Update context for nested breaks */
                        }

                        /* Branch to exit label */
                        acomp_branch_true(exit_label, break_line, break_col);

                        break;  /* Exit case body parsing loop */
                    }

                    if (!parse_statement(ctx)) {
                        return false;
                    }
                }

            } else {
                parser_error("expected integer constant after 'case'");
                return false;
            }

        } else if (token == TOK_DEFAULT) {
            /* Before parsing default, emit SET_LOOP_DEFAULT if we had cases */
            if (has_seen_case && !has_default) {
                acomp_set_loop_default();
            }

            next_token();  /* Consume 'default' */
            has_default = true;

            /* Expect ':' */
            if (!expect_token(TOK_COLON)) {
                return false;
            }

            /* Add and set label for default case */
            int default_label = acomp_add_label();
            acomp_set_label(default_label);

            /* Parse default body statements */
            while (peek_token() != TOK_CASE &&
                   peek_token() != TOK_DEFAULT &&
                   peek_token() != TOK_RBRACE &&
                   peek_token() != TOK_EOF) {

                /* Special handling for break in switch default */
                if (peek_token() == TOK_BREAK) {
                    /* Save break position */
                    int break_line = lexer_get_line();
                    int break_col = lexer_get_column();
                    next_token();  /* Consume 'break' */

                    /* Expect ';' */
                    if (!expect_token(TOK_SEMICOLON)) {
                        return false;
                    }

                    /* Generate break IR pattern */
                    acomp_true();
                    acomp_loop_exit();

                    /* If exit label wasn't created yet (no cases before default), create it */
                    if (!exit_label_created) {
                        exit_label = acomp_add_label();
                        exit_label_created = true;
                        ctx->loop_end_label = exit_label;
                    }

                    /* Branch to exit label */
                    acomp_branch_true(exit_label, break_line, break_col);

                    break;  /* Exit default body parsing loop */
                }

                if (!parse_statement(ctx)) {
                    return false;
                }
            }

        } else {
            /* Unexpected token in switch */
            parser_error("expected 'case' or 'default' in switch");
            return false;
        }
    }

    /* Expect '}' */
    if (!expect_token(TOK_RBRACE)) {
        return false;
    }

    /* Generate final jump sequence (after all cases and default) */
    acomp_true();
    acomp_loop_exit();

    /* If no exit label was created (no breaks), create it now */
    if (!exit_label_created) {
        exit_label = acomp_add_label();
        exit_label_created = true;
    }

    acomp_branch_true(exit_label, switch_line, lparen_col);

    /* Set LOOP_AGAIN marker and set first label for BRANCH_TABLE */
    acomp_loop_again();
    acomp_set_label(first_label);

    /* Generate BRANCH_TABLE instruction */
    acomp_branch_table(switch_line, lparen_col);

    /* Set LOOP_EXIT marker and exit label */
    acomp_loop_exit();
    acomp_set_label(exit_label);

    /* End loop */
    acomp_end_loop();

    /* Restore outer loop labels */
    ctx->loop_start_label = outer_loop_start;
    ctx->loop_end_label = outer_loop_end;
    ctx->loop_start_line = outer_loop_line;
    ctx->loop_start_col = outer_loop_col;

    return true;
}

/**
 * Parse for statement:
 *   for ( init; cond; incr ) statement
 *
 * Complex IR structure with 4 labels:
 * - label 0: loop start (condition check)
 * - label 1: loop exit point
 * - label 2: alternate exit
 * - label 3: increment expression
 */
bool parse_for_statement(ParserContext *ctx) {
    /* Already consumed 'for' keyword */

    /* Expect '(' and save its position */
    if (peek_token() != TOK_LPAREN) {
        parser_error("expected '(' after 'for'");
        return false;
    }

    /* Save position of '(' token BEFORE consuming it
     * Similar to while loop fix - must save after peek but before parse_expression
     */
    int lparen_line = lexer_get_line();
    int lparen_col = lexer_get_column();

    /* Consume '(' */
    next_token();

    /* Parse init expression (executed once before loop) */
    if (peek_token() != TOK_SEMICOLON) {
        if (!parse_expression(ctx)) {
            return false;
        }
    }
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* Create label 0 for loop start (condition check) */
    int loop_start_label = acomp_add_label();

    /* Begin loop */
    acomp_begin_loop();

    /* Set label 0 (loop start) */
    acomp_set_label(loop_start_label);

    /* Parse condition expression */
    if (peek_token() != TOK_SEMICOLON) {
        if (!parse_expression(ctx)) {
            return false;
        }
    }
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* Save position after condition */
    int line = lexer_get_line();
    int col = lexer_get_column();

    /* Create label 1 for loop exit */
    int exit_label = acomp_add_label();

    /* BRANCH_TRUE to exit_label (if condition is FALSE, exit loop) */
    acomp_branch_true(exit_label, line, col);

    /* LOAD_TRUE */
    acomp_true();

    /* LOOP_EXIT marker */
    acomp_loop_exit();

    /* Create label 2 */
    int label2 = acomp_add_label();

    /* BRANCH_TRUE to label2 */
    acomp_branch_true(label2, line, col);

    /* LOOP_AGAIN marker */
    acomp_loop_again();

    /* Create label 3 for increment */
    int increment_label = acomp_add_label();

    /* Set label 3 (increment point) */
    acomp_set_label(increment_label);

    /* Parse increment expression */
    if (peek_token() != TOK_RPAREN) {
        if (!parse_expression(ctx)) {
            return false;
        }
    }

    /* Expect ')' */
    if (!expect_token(TOK_RPAREN)) {
        return false;
    }

    /* LOAD_TRUE */
    acomp_true();

    /* BRANCH_TRUE back to loop_start_label (jump to condition) */
    line = lexer_get_line();
    col = lexer_get_column();
    acomp_branch_true(loop_start_label, line, col);

    /* Set label 1 (exit point) */
    acomp_set_label(exit_label);

    /* Generate OP=0 (STMT_END) */
    acomp_op(0, line, col, 1);

    /* Check if loop body is a block (which needs scope handling) */
    bool body_is_block = (peek_token() == TOK_LBRACE);

    /* Save IR generator's local count before entering loop body (excludes parameters) */
    int body_entry_local_count = ir_get_local_count();

    /* Increment depth and generate NUM_LOCAL before block body */
    if (body_is_block) {
        AcompDepth++;
        acomp_num_local();
    }

    /* Parse loop body */
    bool saved_do_block_scope_managed_by_parent = ctx->block_scope_managed_by_parent;
    ctx->block_scope_managed_by_parent = (peek_token() == TOK_LBRACE);
    if (!parse_statement(ctx)) {
        ctx->block_scope_managed_by_parent = saved_do_block_scope_managed_by_parent;
        return false;
    }
    ctx->block_scope_managed_by_parent = saved_do_block_scope_managed_by_parent;

    /* Generate NUM_LOCAL and decrement depth after block body */
    if (body_is_block) {
        /* Baseline: empty body block omits the "exit" NUM_LOCAL; non-empty
         * body block emits it before the trailing LOAD_TRUE/LOOP_AGAIN.
         */
        if (ctx->last_block_had_statements) {
            acomp_num_local();
        }

        /* If local variables were declared in the block, drop them (baseline emits
         * DROP_LOCAL immediately after the exit NUM_LOCAL, before LOAD_TRUE).
         */
        if (ctx->last_block_had_statements && ir_get_local_count() > body_entry_local_count) {
            acomp_drop_local(body_entry_local_count);
        }
    }

    /* LOAD_TRUE */
    acomp_true();

    /* Return to outer depth after the loop body (see note above) */
    if (body_is_block) {
        AcompDepth--;
    }

    /* LOOP_AGAIN marker */
    acomp_loop_again();

    /* BRANCH_TRUE to increment_label (jump to increment) - use '(' position */
    acomp_branch_true(increment_label, lparen_line, lparen_col);

    /* LOOP_EXIT marker */
    acomp_loop_exit();

    /* Set label 2 */
    acomp_set_label(label2);

    /* End loop */
    acomp_end_loop();

    return true;
}

/**
 * Parse return statement:
 *   return;
 *   return expr;
 */
bool parse_return_statement(ParserContext *ctx, int return_line, int return_col) {
    /* Already consumed 'return' keyword */

    /* Check for return value */
    int first_token = peek_token();
    if (first_token != TOK_SEMICOLON) {
        /* Record the start position of the returned expression (used by
         * else-if chain jump position matching).
         * Note: peek_token() has already loaded the next token and set lexer position.
         */
        ctx->last_return_has_expr = true;
        ctx->last_return_expr_starts_with_identifier = (first_token == TOK_IDENTIFIER);
        ctx->last_return_expr_first_token = first_token;
        ctx->last_return_expr_line = lexer_get_line();
        ctx->last_return_expr_col = lexer_get_column();

        /* Parse return expression */
        if (!parse_expression(ctx)) {
            return false;
        }
    } else {
        /* No return value, return NULL */
        ctx->last_return_has_expr = false;
        ctx->last_return_expr_starts_with_identifier = false;
        ctx->last_return_expr_first_token = 0;
        ctx->last_return_expr_line = 0;
        ctx->last_return_expr_col = 0;
        acomp_null();
    }

    /* Expect ';' - this will set the lexer position to the semicolon */
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* Save position after consuming semicolon for if-statement tracking */
    ctx->last_stmt_end_line = lexer_get_line();
    ctx->last_stmt_end_col = lexer_get_column();
    ctx->last_stmt_was_return = true;  /* Mark that this was a return statement */

    /* Generate return instruction using return keyword position */
    acomp_op(20, return_line, return_col, 1);  /* OP=48 arg1=20 (RETURN), a4=1 for explicit return */

    return true;
}

/**
 * Parse break statement
 */
bool parse_break_statement(ParserContext *ctx, int break_line, int break_col) {
    /* Already consumed 'break' keyword */

    /* Expect ';' */
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* Generate break IR pattern: LOAD_TRUE → LOOP_EXIT → BRANCH_TRUE */
    acomp_true();  /* LOAD_TRUE */
    acomp_loop_exit();  /* LOOP_EXIT marker */

    /* Branch to loop end - use break keyword position */
    acomp_branch_true(ctx->loop_end_label, break_line, break_col);

    return true;
}

/**
 * Parse continue statement
 */
bool parse_continue_statement(ParserContext *ctx, int continue_line, int continue_col) {
    /* Already consumed 'continue' keyword */

    /* Expect ';' */
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* Generate continue IR pattern: LOAD_TRUE → LOOP_AGAIN → BRANCH_TRUE */
    acomp_true();  /* LOAD_TRUE */
    acomp_loop_again();  /* LOOP_AGAIN marker */

    /* Branch to loop start - use continue keyword position */
    acomp_branch_true(ctx->loop_start_label, continue_line, continue_col);

    return true;
}

/**
 * Parse expression statement:
 *   expr;
 */
bool parse_expression_statement(ParserContext *ctx) {
    /* Parse expression */
    if (!parse_expression(ctx)) {
        return false;
    }

    /* Expect ';' - this will set the lexer position to the semicolon */
    if (!expect_token(TOK_SEMICOLON)) {
        return false;
    }

    /* Save position after consuming semicolon (which points to the semicolon token) */
    int stmt_line = lexer_get_line();
    int stmt_col = lexer_get_column();

    /* Generate statement end */
    ctx->last_stmt_end_line = stmt_line;
    ctx->last_stmt_end_col = stmt_col;
    ctx->last_stmt_was_return = false;  /* This is not a return statement */

    if (ctx->suppress_next_stmt_end) {
        ctx->suppress_next_stmt_end = false;
        return true;
    }

    acomp_op(0, stmt_line, stmt_col, 1);  /* OP=48 arg1=0 (STMT_END) a4=1 */

    return true;
}

/**
 * Parse statement
 */
bool parse_statement(ParserContext *ctx) {
    /* Statement-level reset for per-statement quirks */
    ctx->function_end_override_valid = false;
    ctx->suppress_next_stmt_end = false;

    int token = peek_token();

    switch (token) {
        case TOK_LBRACE:
            if (ctx->block_scope_managed_by_parent) {
                return parse_block_statement(ctx);
            }

            /* Standalone block statement introduces a scope in baseline IR. */
            {
                int entry_local_count = ir_get_local_count();
                AcompDepth++;
                acomp_num_local();

                if (!parse_block_statement(ctx)) {
                    return false;
                }

                /* Baseline quirk: empty blocks emit only the entry NUM_LOCAL. */
                if (ctx->last_block_had_statements) {
                    acomp_num_local();
                }

                if (ctx->last_block_had_statements && ir_get_local_count() > entry_local_count) {
                    acomp_drop_local(entry_local_count);
                }

                AcompDepth--;
                return true;
            }

        case TOK_IF:
            next_token();
            return parse_if_statement(ctx);

        case TOK_WHILE:
            next_token();
            return parse_while_statement(ctx);

        case TOK_FOR:
            next_token();
            return parse_for_statement(ctx);

        case TOK_DO:
            next_token();
            return parse_do_while_statement(ctx);

        case TOK_SWITCH: {
            /* Save switch keyword position (column will be 0) */
            int switch_line = lexer_get_line();
            int switch_col_initial = lexer_get_column();
            next_token();  /* Consume 'switch' */
            return parse_switch_statement(ctx, switch_line, switch_col_initial);
        }

        case TOK_RETURN: {
            /* Save return keyword position before consuming */
            int return_line = lexer_get_line();
            int return_col = lexer_get_column();
            next_token();
            return parse_return_statement(ctx, return_line, return_col);
        }

        case TOK_BREAK: {
            int break_line = lexer_get_line();
            int break_col = lexer_get_column();
            next_token();
            return parse_break_statement(ctx, break_line, break_col);
        }

        case TOK_CONTINUE: {
            int continue_line = lexer_get_line();
            int continue_col = lexer_get_column();
            next_token();
            return parse_continue_statement(ctx, continue_line, continue_col);
        }

        case TOK_DECL:
            next_token();
            return parse_decl_statement(ctx);

        case TOK_SEMICOLON:
            /* Empty statement */
            next_token();
            return true;

        default:
            /* Expression statement */
            return parse_expression_statement(ctx);
    }
}

/* Continued in next file... */
