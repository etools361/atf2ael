/*
 * yacc_parser_globals.h
 * Global variable declarations for YACC parser and new parser
 */

#ifndef YACC_PARSER_GLOBALS_H
#define YACC_PARSER_GLOBALS_H

#include <stdio.h>
#include <stdint.h>

/* ========================================
 * Type Definitions
 * ======================================== */

/* IDA Pro type aliases */
typedef int _DWORD;

/* 128-bit emulation structure (for semantic values) */
typedef struct {
    int64_t low;
    int64_t high;
} __int128_emu;

/* ========================================
 * Global Variables
 * ======================================== */

/* Parser state */
extern int dword_18007ED74;      /* Token hint for lexer */
extern int dword_18007E890;      /* Lookahead token */
extern int dword_18007E898;      /* Error count */

/* Semantic values */
extern __int128_emu xmmword_18007EE88;
extern __int128_emu agram_lval;

/* Other parser control variables */
extern int dword_18007FA60;
extern int dword_18007FA64;
extern int64_t qword_18007EE98;
extern int64_t qword_18007FA58;
extern int dword_18007FA68;
extern int dword_18007E89C;
extern int64_t qword_18007E8A0;
extern int64_t qword_18007E8A8;
extern int dword_18007ECB4;
extern int dword_18007ECB8;
extern int dword_18007ED14;
extern int dword_18007ED18;
extern int dword_18007ED70;
extern int dword_18007ED78;
extern int dword_18007EE84;

/* State stacks */
extern int dword_18007F800[2000];
extern char unk_18007EEA0[16000];

/* Location stacks */
extern int dword_18007ED20[10];
extern int dword_18007ED48[10];
extern int dword_18007ECC0[10];
extern int dword_18007ECE8[10];
extern int dword_18007ED44;
extern int dword_18007ED6C;
extern int dword_18007ED0C;
extern int dword_18007ECE4;

/* Special buffers */
extern char unk_18007E8B0[256];
extern int Destination;  /* Changed from char array to int flag */
extern char AcompDefaultVoc[256];
extern const char *AcompCurrentVoc;  /* Changed from int64_t to char* */

/* Special variables */
extern int64_t currentExpr;
extern void *off_180076348;
extern char byte_18007EE80;
extern int dword_18007ED10;

/* File stream */
extern FILE *ascan_stream;

/* ========================================
 * Function Declarations
 * ======================================== */

/* YACC parser functions (stubs for new parser) */
int64_t yacc_parser(void);
void yacc_parser_init_tables(void);

#endif /* YACC_PARSER_GLOBALS_H */
