/*
 * ael_parser_new.h
 * Clear, maintainable AEL Parser
 *
 * This parser uses recursive descent parsing with clear variable names
 * and calls the existing IR generation functions (acomp_xxx)
 */

#ifndef AEL_PARSER_NEW_H
#define AEL_PARSER_NEW_H

#include <stdbool.h>
#include <stdint.h>

/* Token types - matching ascan_lex token values */
typedef enum {
    TOK_EOF = 0,
    TOK_LPAREN = 40,        // (
    TOK_RPAREN = 41,        // )
    TOK_COMMA = 44,         // ,
    TOK_SEMICOLON = 59,     // ;
    TOK_LBRACE = 123,       // {
    TOK_RBRACE = 125,       // }
    TOK_LBRACKET = 91,      // [
    TOK_RBRACKET = 93,      // ]

    /* Keywords */
    TOK_DECL = 257,
    TOK_DEFUN = 258,
    TOK_IF = 259,
    TOK_ELSE = 260,
    TOK_WHILE = 261,
    TOK_FOR = 262,
    TOK_RETURN = 263,
    TOK_BREAK = 264,
    TOK_CONTINUE = 265,
    TOK_TRUE = 266,
    TOK_FALSE = 267,
    TOK_NULL = 268,
    TOK_DO = 269,
    TOK_SWITCH = 270,
    TOK_CASE = 271,
    TOK_DEFAULT = 272,

    /* Literals */
    TOK_INTEGER = 273,
    TOK_REAL = 274,
    TOK_IMAG = 275,
    TOK_STRING = 276,
    TOK_IDENTIFIER = 277,

    /* Operators */
    TOK_ASSIGN = 61,        // =
    TOK_PLUS = 43,          // +
    TOK_MINUS = 45,         // -
    TOK_STAR = 42,          // *
    TOK_SLASH = 47,         // /
    TOK_PERCENT = 37,       // %
    TOK_LT = 60,            // <
    TOK_GT = 62,            // >
    TOK_EQ = 280,           // ==
    TOK_NE = 281,           // !=
    TOK_LE = 282,           // <=
    TOK_GE = 283,           // >=
    TOK_AND = 284,          // &&
    TOK_OR = 285,           // ||
    TOK_NOT = 33,           // !
    TOK_QUESTION = 63,      // ?
    TOK_COLON = 58,         // :
    TOK_BIT_AND = 38,       // &
    TOK_BIT_OR = 124,       // |
    TOK_BIT_XOR = 94,       // ^
    TOK_LSHIFT = 290,       // <<
    TOK_RSHIFT = 291,       // >>

    /* Compound assignment operators */
    TOK_PLUS_ASSIGN = 295,  // +=
    TOK_MINUS_ASSIGN = 296, // -=
    TOK_STAR_ASSIGN = 297,  // *=
    TOK_SLASH_ASSIGN = 298, // /=
    TOK_PERCENT_ASSIGN = 299, // %=
    TOK_INCREMENT = 300,    // ++
    TOK_DECREMENT = 301,    // --

    /* Power operator */
    TOK_POWER = 302,        // **
} TokenType;

/* Parser context */
typedef struct {
    int current_token;          /* Current token from lexer */
    int lookahead_valid;        /* Whether lookahead token is valid */
    bool in_function;           /* Are we inside a function definition? */
    int function_depth;         /* Nesting depth (0=global, 1=function) */
    int local_var_count;        /* Count of local variables in current function */
    bool had_error;             /* Did we encounter any errors? */
    int error_count;            /* Number of errors */
    int last_rbrace_line;       /* Line number of last } consumed */
    int last_rbrace_col;        /* Column number of last } consumed */
    int last_lparen_line;       /* Line number of last ( consumed (for ternary) */
    int last_lparen_col;        /* Column number of last ( consumed (for ternary) */
    int last_stmt_end_line;     /* Line number of last statement end (OP=0) */
    int last_stmt_end_col;      /* Column number of last statement end (OP=0) */
    bool last_stmt_was_return;  /* Was the last statement a return? */
    bool last_return_has_expr;  /* Was the last return `return expr;` ? */
    bool last_return_expr_starts_with_identifier; /* Return expr starts with identifier token */
    int last_return_expr_first_token; /* First token type of return expr (from peek_token) */
    int last_return_expr_line;  /* Start position of returned expression */
    int last_return_expr_col;

    /* Empty list literal "{}" quirks (baseline IR compatibility) */
    bool last_expr_was_empty_brace_list;   /* Last parsed primary expr was "{}" */
    int last_empty_brace_list_line;        /* Position of '{' in "{}" */
    int last_empty_brace_list_col;
    bool suppress_next_stmt_end;           /* Suppress OP=0 emission for next expr stmt */
    bool function_end_override_valid;      /* Override implicit return/DEFINE_FUNCT position */
    int function_end_override_line;
    int function_end_override_col;

    /* Block parsing helpers */
    bool last_block_had_statements;        /* Most recently parsed {...} had any statements */
    bool block_scope_managed_by_parent;    /* Parent already emitted scope NUM_LOCAL/depth */

    /* Loop control - for break/continue */
    int loop_start_label;       /* Label to jump to for continue */
    int loop_end_label;         /* Label to jump to for break */
    int loop_start_line;        /* Position of loop start (for BRANCH_TRUE) */
    int loop_start_col;
} ParserContext;

/* Main parser functions */
bool parse_ael_program(void);
bool parse_global_statement(ParserContext *ctx);
bool parse_statement(ParserContext *ctx);

/* Declaration parsing */
bool parse_decl_statement(ParserContext *ctx);
bool parse_function_definition(ParserContext *ctx, int defun_line);

/* Expression parsing */
bool parse_expression(ParserContext *ctx);
bool parse_assignment_expr(ParserContext *ctx);
bool parse_logical_or_expr(ParserContext *ctx);
bool parse_logical_and_expr(ParserContext *ctx);
bool parse_equality_expr(ParserContext *ctx);
bool parse_relational_expr(ParserContext *ctx);
bool parse_additive_expr(ParserContext *ctx);
bool parse_multiplicative_expr(ParserContext *ctx);
bool parse_unary_expr(ParserContext *ctx);
bool parse_postfix_expr(ParserContext *ctx);
bool parse_primary_expr(ParserContext *ctx);

/* Statement parsing */
bool parse_block_statement(ParserContext *ctx);
bool parse_if_statement(ParserContext *ctx);
bool parse_while_statement(ParserContext *ctx);
bool parse_do_while_statement(ParserContext *ctx);
bool parse_for_statement(ParserContext *ctx);
bool parse_switch_statement(ParserContext *ctx, int switch_line, int switch_col);
bool parse_return_statement(ParserContext *ctx, int return_line, int return_col);
bool parse_break_statement(ParserContext *ctx, int break_line, int break_col);
bool parse_continue_statement(ParserContext *ctx, int continue_line, int continue_col);
bool parse_expression_statement(ParserContext *ctx);

/* Function call parsing */
bool parse_function_call(ParserContext *ctx, const char *func_name);
bool parse_function_call_with_pos(ParserContext *ctx, const char *func_name, int func_line, int func_col);
bool parse_argument_list(ParserContext *ctx, int *arg_count);

/* Utility functions */
int next_token(void);           /* Get next token from lexer */
int peek_token(void);           /* Peek at current token without consuming */
bool expect_token(int expected_token);  /* Consume expected token or error */
void parser_error(const char *message);  /* Report parser error */
const char *token_name(int token);  /* Get token name for error messages */

#endif /* AEL_PARSER_NEW_H */
