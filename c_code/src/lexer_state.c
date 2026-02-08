/*
 * lexer_state.c
 * Global state for lexer to share identifier/string values with parser
 */

#include <string.h>

/* Global buffers for lexer values */
char g_lexer_identifier[256] = {0};
char g_lexer_string[1024] = {0};
int g_lexer_int_value = 0;
double g_lexer_real_value = 0.0;

/* Get last identifier scanned */
const char* lexer_get_identifier(void) {
    return g_lexer_identifier;
}

/* Get last string scanned */
const char* lexer_get_string(void) {
    return g_lexer_string;
}

/* Get last integer scanned */
int lexer_get_int(void) {
    return g_lexer_int_value;
}

/* Get last real number scanned */
double lexer_get_real(void) {
    return g_lexer_real_value;
}

/* Set identifier (called by lexer) */
void lexer_set_identifier(const char* id) {
    strncpy(g_lexer_identifier, id, sizeof(g_lexer_identifier) - 1);
    g_lexer_identifier[sizeof(g_lexer_identifier) - 1] = '\0';
}

/* Set string (called by lexer) */
void lexer_set_string(const char* str) {
    /* Baseline behavior: long string literals are truncated to a fixed limit. */
    enum { AEL_MAX_STRING_CHARS = 510 };
    size_t len = str ? strlen(str) : 0;
    if (len > AEL_MAX_STRING_CHARS) len = AEL_MAX_STRING_CHARS;
    if (len > 0) {
        memcpy(g_lexer_string, str, len);
    }
    g_lexer_string[len] = '\0';
}

/* Set integer (called by lexer) */
void lexer_set_int(int val) {
    g_lexer_int_value = val;
}

/* Set real (called by lexer) */
void lexer_set_real(double val) {
    g_lexer_real_value = val;
}

/* Position tracking (for arg2/arg3 in acomp_op) */
static int g_current_line = 1;
static int g_current_column = 1;

int lexer_get_line(void) {
    /* Return 0-based line number to match baseline IR */
    return g_current_line > 0 ? g_current_line - 1 : 0;
}

int lexer_get_column(void) {
    /* Return 0-based column number to match baseline IR */
    return g_current_column > 0 ? g_current_column - 1 : 0;
}

void lexer_set_position(int line, int column) {
    g_current_line = line;
    g_current_column = column;
}
