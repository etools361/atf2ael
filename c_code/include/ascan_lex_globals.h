/*
 * ascan_lex_globals.h
 * Global declarations for ascan_lex (decompiled lexer)
 */

#ifndef ASCAN_LEX_GLOBALS_H
#define ASCAN_LEX_GLOBALS_H

/* Include yacc_parser_globals.h for type definitions and agram_lval */
#include "yacc_parser_globals.h"

/* ========================================
 * Lexer Global Variables
 * ======================================== */

/* Lexer state variables (defined in ascan_lex_stubs.c) */
extern int dword_18007A9A0;       /* Lexer state flag */
extern int dword_180080218;       /* Lexer mode/state */
extern int dword_180080264;       /* Token type */
extern int dword_180080268;       /* Token line number */
extern int dword_18008026C;       /* Token column */
extern int dword_180080270;       /* Token flags */
extern int dword_180080274;       /* Additional token info */
extern int dword_1800801E0;       /* Error state */
extern __int64 qword_1800801E8;   /* Current file pointer */
extern __int64 qword_1800801F0;   /* String buffer pointer */
extern __int64 qword_180080200;   /* Current position */
extern __int64 qword_180080210;   /* Buffer pointer */
extern __int64 qword_180080228;   /* Token value pointer */
extern int dword_18008020C;       /* String pointer cast */
extern int dword_180080220;       /* Line number */
extern char byte_180080208;       /* Character buffer */

/* Lookup tables (should be defined in ascan_lex.c or external table file) */
extern unsigned char byte_18005A6D0[512];   /* Character class table */
extern unsigned short word_18005A4D0[512];  /* Token type table */
extern unsigned char byte_18005AAD0[512];   /* Char property table */
extern unsigned short word_18005B390[512];  /* Symbol table 1 */
extern unsigned short word_18005ABD0[512];  /* Symbol table 2 */
extern unsigned short word_18005AFF0[512];  /* Action table */

/* External string */
extern char *String;  /* Global string buffer (from parser) */

/* ========================================
 * External Functions
 * ======================================== */

/* From standard library or runtime */
FILE *_acrt_iob_func(unsigned int ix);

/* From compiler_progressive.c or other modules */
__int64 sub_18002D360(void *file);
__int64 sub_1800483B0(void *ctx, const char *str, int len);

#endif /* ASCAN_LEX_GLOBALS_H */
