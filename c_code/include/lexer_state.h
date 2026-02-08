/*
 * lexer_state.h
 * Global state for lexer to share values with parser
 */

#ifndef LEXER_STATE_H
#define LEXER_STATE_H

/* Get functions (used by parser) */
const char* lexer_get_identifier(void);
const char* lexer_get_string(void);
int lexer_get_int(void);
double lexer_get_real(void);

/* Position tracking for IR generation (arg2/arg3 in acomp_op) */
int lexer_get_line(void);
int lexer_get_column(void);

/* Set functions (used by lexer) */
void lexer_set_identifier(const char* id);
void lexer_set_string(const char* str);
void lexer_set_int(int val);
void lexer_set_real(double val);
void lexer_set_position(int line, int column);

#endif /* LEXER_STATE_H */
