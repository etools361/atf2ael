/*
 * parser_globals.c
 * Global variable definitions for the new parser
 */

#include "yacc_parser_globals.h"

/* ========================================
 * Parser Global Variables
 * ======================================== */

/* Parser state */
int dword_18007ED74 = 257;      /* Token hint for lexer */
int dword_18007E890 = -1;       /* Lookahead token */
int dword_18007E898 = 0;        /* Error count */

/* Semantic values */
__int128_emu xmmword_18007EE88 = {0, 0};
__int128_emu agram_lval = {0, 0};

/* Other parser control variables */
int dword_18007FA60 = 0;
int dword_18007FA64 = 0;
__int64 qword_18007EE98 = 0;
__int64 qword_18007FA58 = 0;
int dword_18007FA68 = 0;
int dword_18007E89C = 0;
__int64 qword_18007E8A0 = 0;
__int64 qword_18007E8A8 = 0;
int dword_18007ECB4 = 0;
int dword_18007ECB8 = 0;
int dword_18007ED14 = 0;
int dword_18007ED18 = 0;
int dword_18007ED70 = 0;
int dword_18007ED78 = 0;
int dword_18007EE84 = 0;

/* State stacks */
int dword_18007F800[2000] = {0};
char unk_18007EEA0[16000] = {0};

/* Location stacks */
int dword_18007ED20[10] = {0};
int dword_18007ED48[10] = {0};
int dword_18007ECC0[10] = {0};
int dword_18007ECE8[10] = {0};
int dword_18007ED44 = 0;
int dword_18007ED6C = 0;
int dword_18007ED0C = 0;
int dword_18007ECE4 = 0;

/* Special buffers */
char unk_18007E8B0[256] = {0};
int Destination = 0;  /* Changed from char array to int flag */
char AcompDefaultVoc[256] = {0};
const char *AcompCurrentVoc = NULL;  /* Changed from __int64 to char* */

/* Special variables */
__int64 currentExpr = 0;
void *off_180076348 = NULL;
char byte_18007EE80 = 0;
int dword_18007ED10 = 0;

/* File stream (from lexer) - declared extern, defined in compiler_progressive.c */
extern FILE *ascan_stream;

/* Dummy yacc_parser function for compiler_progressive.c */
__int64 yacc_parser(void) {
    /* This function is not used with the new parser */
    /* It's only here to satisfy the linker */
    return 0;
}

/* yacc_parser_init_tables - Initialize YACC parser tables */
void yacc_parser_init_tables(void) {
    /* Tables are already defined statically in yacc_parser_tables.c */
    /* This is just a stub for compatibility */
}

