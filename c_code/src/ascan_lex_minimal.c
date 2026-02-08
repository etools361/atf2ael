/*
 * ascan_lex_minimal.c
 * Minimal AEL Lexical Analyzer
 *
 * This is a simplified version to verify the parser integration.
 * Full 4000-line version will be integrated later if needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include "ael_debug.h"
#include "yacc_parser_globals.h"
#include "ael_parser_new.h"  /* Use parser's token definitions */
#include "lexer_state.h"      /* For sharing values with parser */

/* Treat lexer printf as debug-only output. */
#define printf AEL_DEBUG_PRINTF

/* ========================================
 * Global State
 * ======================================== */

extern __int128_emu agram_lval;  /* Current semantic value */

/* Lexer state */
static FILE *lex_input = NULL;
static int lex_line = 1;
static int lex_col = 1;
static char lex_buffer[1024];
static int lex_buf_pos = 0;

/* Token start position (saved before scanning) */
static int token_start_line = 1;
static int token_start_col = 1;

/* Macro to return token with position tracking */
#define RETURN_TOKEN(tok) do { \
    lexer_set_position(token_start_line, token_start_col); \
    return (tok); \
} while(0)

/* ========================================
 * Keyword Table (using parser's token values)
 * ======================================== */

struct keyword {
    const char *name;
    int token;
};

static struct keyword keywords[] = {
    {"decl", TOK_DECL},
    {"defun", TOK_DEFUN},
    {"return", TOK_RETURN},
    {"if", TOK_IF},
    {"else", TOK_ELSE},
    {"while", TOK_WHILE},
    {"for", TOK_FOR},
    {"do", TOK_DO},
    {"switch", TOK_SWITCH},
    {"case", TOK_CASE},
    {"default", TOK_DEFAULT},
    {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"TRUE", TOK_TRUE},      // uppercase
    {"FALSE", TOK_FALSE},    // uppercase
    {"null", TOK_NULL},      // lowercase
    {"NULL", TOK_NULL},      // uppercase
    {NULL, 0}
};

/* ========================================
 * Helper Functions
 * ======================================== */

static double ael_strtod_c(const char *s)
{
#if defined(_WIN32)
    static _locale_t c_locale = NULL;
    if (!c_locale) {
        c_locale = _create_locale(LC_NUMERIC, "C");
    }
    if (c_locale) {
        return _strtod_l(s, NULL, c_locale);
    }
#endif
    return strtod(s, NULL);
}

static int get_char(void)
{
    int ch = fgetc(lex_input);
    if (ch == '\n') {
        lex_line++;
        lex_col = 1;
    } else if (ch == '\t') {
        /* Expand tab to next 4-column boundary (tab stops at 4, 8, 12, ...) */
        lex_col = ((lex_col - 1) / 4 + 1) * 4 + 1;
    } else if (ch != EOF) {
        lex_col++;
    }
    return ch;
}

static void unget_char(int ch)
{
    if (ch != EOF) {
        ungetc(ch, lex_input);
        if (ch == '\n') {
            lex_line--;
        } else if (ch == '\t') {
            /* Reverse tab expansion: move back to previous tab boundary
             * Since we don't know the original column, we approximate by moving back 4 columns
             * This is a limitation of the unget mechanism, but in practice tokens rarely unget tabs
             */
            lex_col = (lex_col > 4) ? lex_col - 4 : 1;
        } else {
            lex_col--;
        }
    }
}

static void skip_whitespace(void)
{
    int ch;
    while ((ch = get_char()) != EOF) {
        if (!isspace(ch)) {
            unget_char(ch);
            break;
        }
    }
}

static void skip_comment(void)
{
    int ch;
    /* Skip until end of line */
    while ((ch = get_char()) != EOF && ch != '\n')
        ;
    if (ch == '\n')
        unget_char(ch);
}

static void skip_block_comment(void)
{
    int ch;
    /* Skip until closing asterisk-slash found */
    while ((ch = get_char()) != EOF) {
        if (ch == '*') {
            int next = get_char();
            if (next == '/') {
                /* End of block comment */
                return;
            } else {
                unget_char(next);
            }
        }
    }
}

static int check_keyword(const char *str)
{
    for (int i = 0; keywords[i].name != NULL; i++) {
        if (strcmp(str, keywords[i].name) == 0) {
            return keywords[i].token;
        }
    }
    RETURN_TOKEN(TOK_IDENTIFIER);
}

/* ========================================
 * Main Lexical Analyzer
 * ======================================== */

int ascan_lex(int token_hint)
{
    int ch;

    /* Initialize on first call */
    if (!lex_input) {
        extern FILE *ascan_stream;
        lex_input = ascan_stream;
        if (!lex_input) {
            fprintf(stderr, "[ascan_lex] Error: Input stream not initialized\n");
            RETURN_TOKEN(TOK_EOF);
        }
        printf("[ascan_lex] Initialized, reading from stream\n");
    }

    /* Skip whitespace and comments */
    while (1) {
        skip_whitespace();

        /* Save token start position BEFORE reading the token */
        token_start_line = lex_line;
        token_start_col = lex_col;

        ch = get_char();

        if (ch == EOF) {
            printf("[ascan_lex] EOF reached\n");
            RETURN_TOKEN(TOK_EOF);
        }

        /* Check for comment or division */
        if (ch == '/') {
            int next = get_char();
            if (next == '/') {
                /* Single-line comment */
                skip_comment();
                continue;
            } else if (next == '*') {
                /* Block comment */
                skip_block_comment();
                continue;
            } else if (next == '=') {
                printf("[ascan_lex] Token: SLASH_ASSIGN '/='\n");
                RETURN_TOKEN(TOK_SLASH_ASSIGN);
            } else {
                unget_char(next);
                /* Division operator */
                printf("[ascan_lex] Token: SLASH '/'\n");
                RETURN_TOKEN(TOK_SLASH);
            }
        }

        break;
    }

    /* Single-character tokens */
    switch (ch) {
        case '{':
            printf("[ascan_lex] Token: LBRACE '{'\n");
            RETURN_TOKEN(TOK_LBRACE);
        case '}':
            printf("[ascan_lex] Token: RBRACE '}'\n");
            RETURN_TOKEN(TOK_RBRACE);
        case '(':
            printf("[ascan_lex] Token: LPAREN '('\n");
            RETURN_TOKEN(TOK_LPAREN);
        case ')':
            printf("[ascan_lex] Token: RPAREN ')'\n");
            RETURN_TOKEN(TOK_RPAREN);
        case '[':
            printf("[ascan_lex] Token: LBRACKET '['\n");
            RETURN_TOKEN(TOK_LBRACKET);
        case ']':
            printf("[ascan_lex] Token: RBRACKET ']'\n");
            RETURN_TOKEN(TOK_RBRACKET);
        case ',':
            printf("[ascan_lex] Token: COMMA ','\n");
            RETURN_TOKEN(TOK_COMMA);
        case ';':
            printf("[ascan_lex] Token: SEMICOLON ';'\n");
            RETURN_TOKEN(TOK_SEMICOLON);
        case '=': {
            int next = get_char();
            if (next == '=') {
                printf("[ascan_lex] Token: EQ '=='\n");
                RETURN_TOKEN(TOK_EQ);
            } else {
                unget_char(next);
                printf("[ascan_lex] Token: ASSIGN '='\n");
                RETURN_TOKEN(TOK_ASSIGN);
            }
        }
        case '+': {
            int next = get_char();
            if (next == '+') {
                printf("[ascan_lex] Token: INCREMENT '++'\n");
                RETURN_TOKEN(TOK_INCREMENT);
            } else if (next == '=') {
                printf("[ascan_lex] Token: PLUS_ASSIGN '+='\n");
                RETURN_TOKEN(TOK_PLUS_ASSIGN);
            } else {
                unget_char(next);
                printf("[ascan_lex] Token: PLUS '+'\n");
                RETURN_TOKEN(TOK_PLUS);
            }
        }
        case '-': {
            int next = get_char();
            if (next == '-') {
                printf("[ascan_lex] Token: DECREMENT '--'\n");
                RETURN_TOKEN(TOK_DECREMENT);
            } else if (next == '=') {
                printf("[ascan_lex] Token: MINUS_ASSIGN '-='\n");
                RETURN_TOKEN(TOK_MINUS_ASSIGN);
            } else {
                unget_char(next);
                printf("[ascan_lex] Token: MINUS '-'\n");
                RETURN_TOKEN(TOK_MINUS);
            }
        }
        case '*': {
            int next = get_char();
            if (next == '=') {
                printf("[ascan_lex] Token: STAR_ASSIGN '*='\n");
                RETURN_TOKEN(TOK_STAR_ASSIGN);
            } else if (next == '*') {
                printf("[ascan_lex] Token: POWER '**'\n");
                RETURN_TOKEN(TOK_POWER);
            } else {
                unget_char(next);
                printf("[ascan_lex] Token: STAR '*'\n");
                RETURN_TOKEN(TOK_STAR);
            }
        }
        case '%': {
            int next = get_char();
            if (next == '=') {
                printf("[ascan_lex] Token: PERCENT_ASSIGN '%%='\n");
                RETURN_TOKEN(TOK_PERCENT_ASSIGN);
            } else {
                unget_char(next);
                printf("[ascan_lex] Token: PERCENT '%%'\n");
                RETURN_TOKEN(TOK_PERCENT);
            }
        }
        case '<': {
            int next = get_char();
            if (next == '=') {
                RETURN_TOKEN(TOK_LE);
            } else if (next == '<') {
                RETURN_TOKEN(TOK_LSHIFT);  // <<
            } else {
                unget_char(next);
                RETURN_TOKEN(TOK_LT);
            }
        }
        case '>': {
            int next = get_char();
            if (next == '=') {
                RETURN_TOKEN(TOK_GE);
            } else if (next == '>') {
                RETURN_TOKEN(TOK_RSHIFT);  // >>
            } else {
                unget_char(next);
                RETURN_TOKEN(TOK_GT);
            }
        }
        case '!': {
            int next = get_char();
            if (next == '=') {
                RETURN_TOKEN(TOK_NE);
            } else {
                unget_char(next);
                RETURN_TOKEN(TOK_NOT);
            }
        }
        case '&': {
            int next = get_char();
            if (next == '&') {
                RETURN_TOKEN(TOK_AND);
            } else {
                unget_char(next);
                RETURN_TOKEN(TOK_BIT_AND);  // Single &
            }
        }
        case '|': {
            int next = get_char();
            if (next == '|') {
                RETURN_TOKEN(TOK_OR);
            } else {
                unget_char(next);
                RETURN_TOKEN(TOK_BIT_OR);  // Single |
            }
        }
        case '^':
            RETURN_TOKEN(TOK_BIT_XOR);
        case '?':
            RETURN_TOKEN(TOK_QUESTION);
        case ':':
            RETURN_TOKEN(TOK_COLON);
        case '"': {
            /* String literal - preserve escape sequences for IR output */
            int i = 0;
            while ((ch = get_char()) != EOF && ch != '"') {
                if (ch == '\\') {
                    int next = get_char();
                    if (next == '\n') {
                        /* Line continuation inside string: "\" + newline is removed. */
                        continue;
                    }
                    if (next == '\r') {
                        /* Handle CRLF continuation if it ever reaches lexer. */
                        int next2 = get_char();
                        if (next2 != '\n') {
                            unget_char(next2);
                        }
                        continue;
                    }
                    /* Keep the backslash and the escaped char as-is */
                    if (i < sizeof(lex_buffer) - 1) {
                        lex_buffer[i++] = '\\';
                    }
                    if (next != EOF && i < sizeof(lex_buffer) - 1) {
                        lex_buffer[i++] = next;
                    }
                } else {
                    if (i < sizeof(lex_buffer) - 1) {
                        lex_buffer[i++] = ch;
                    }
                }
            }
            lex_buffer[i] = '\0';
            printf("[ascan_lex] Token: STRING \"%s\"\n", lex_buffer);
            lexer_set_string(lex_buffer);
            RETURN_TOKEN(TOK_STRING);
        }
    }

    /* Identifier or keyword */
    if (isalpha(ch) || ch == '_') {
        int i = 0;
        lex_buffer[i++] = ch;

        while ((ch = get_char()) != EOF) {
            if (isalnum(ch) || ch == '_') {
                if (i < sizeof(lex_buffer) - 1) {
                    lex_buffer[i++] = ch;
                }
            } else {
                unget_char(ch);
                break;
            }
        }

        lex_buffer[i] = '\0';

        int token = check_keyword(lex_buffer);
        if (token == TOK_IDENTIFIER) {
            printf("[ascan_lex] Token: IDENTIFIER '%s'\n", lex_buffer);
            lexer_set_identifier(lex_buffer);  /* Store for parser */
        } else {
            printf("[ascan_lex] Token: KEYWORD '%s' (token=%d)\n", lex_buffer, token);
        }
        /* Set position for all identifier/keyword tokens */
        lexer_set_position(token_start_line, token_start_col);
        return token;
    }

    /* Number */
    if (isdigit(ch)) {
        int i = 0;
        bool has_dot = false;
        bool has_exp = false;
        lex_buffer[i++] = ch;

        while ((ch = get_char()) != EOF) {
            if (isdigit(ch)) {
                if (i < sizeof(lex_buffer) - 1) {
                    lex_buffer[i++] = ch;
                }
            } else if (ch == '.' && !has_dot && !has_exp) {
                has_dot = true;
                if (i < sizeof(lex_buffer) - 1) {
                    lex_buffer[i++] = ch;
                }
            } else if ((ch == 'e' || ch == 'E') && !has_exp) {
                /* Scientific notation */
                has_exp = true;
                if (i < sizeof(lex_buffer) - 1) {
                    lex_buffer[i++] = ch;
                }
                /* Check for sign after 'e' */
                int next = get_char();
                if (next == '+' || next == '-') {
                    if (i < sizeof(lex_buffer) - 1) {
                        lex_buffer[i++] = next;
                    }
                } else {
                    unget_char(next);
                }
            } else {
                unget_char(ch);
                break;
            }
        }

        lex_buffer[i] = '\0';

        /* Check for imaginary suffix 'i' */
        int next_ch = get_char();
        if (next_ch == 'i') {
            /* Imaginary number */
            double imag_val = ael_strtod_c(lex_buffer);
            lexer_set_real(imag_val);  /* Store coefficient */
            printf("[ascan_lex] Token: IMAG '%si'\n", lex_buffer);
            RETURN_TOKEN(TOK_IMAG);
        } else {
            unget_char(next_ch);
        }

        /* Store number value */
        if (has_dot || has_exp) {
            lexer_set_real(ael_strtod_c(lex_buffer));
            printf("[ascan_lex] Token: REAL '%s'\n", lex_buffer);
            RETURN_TOKEN(TOK_REAL);
        } else {
            /* Check if integer is too large for int type */
            errno = 0;
            long long val = strtoll(lex_buffer, NULL, 10);
            if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
                /* Integer overflow - treat as real number */
                lexer_set_real(ael_strtod_c(lex_buffer));
                printf("[ascan_lex] Token: REAL '%s' (overflow, treated as real)\n", lex_buffer);
                RETURN_TOKEN(TOK_REAL);
            } else {
                lexer_set_int((int)val);
                printf("[ascan_lex] Token: INTEGER '%s'\n", lex_buffer);
                RETURN_TOKEN(TOK_INTEGER);
            }
        }
    }

    /* Unknown character */
    printf("[ascan_lex] Warning: Unknown character '%c' (ASCII %d)\n", ch, ch);
    return ch;  /* Return ASCII value */
}

/* ========================================
 * Lexer Control Functions
 * ======================================== */

void ascan_lex_init(FILE *fp)
{
    lex_input = fp;
    lex_line = 1;
    lex_col = 1;
    lex_buf_pos = 0;
}

void ascan_lex_reset(void)
{
    lex_input = NULL;
    lex_line = 1;
    lex_col = 1;
    lex_buf_pos = 0;
}

int ascan_lex_get_line(void)
{
    return lex_line;
}

int ascan_lex_get_col(void)
{
    return lex_col;
}
