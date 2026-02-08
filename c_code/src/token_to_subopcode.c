/*
 * token_to_subopcode.c
 * Token to IR Subopcode Mapping Implementation
 */

#include "token_to_subopcode.h"
#include "ael_parser_new.h"
#include <stdio.h>

/* Convert lexer token type to OP=48 sub-opcode */
int16_t token_to_subopcode(int token_type) {
    switch (token_type) {
        /* Binary arithmetic operators */
        case TOK_PLUS:     return 10;  // ADD
        case TOK_MINUS:    return 11;  // SUB
        case TOK_STAR:     return 12;  // MUL
        case TOK_SLASH:    return 14;  // DIV (was 13)
        case TOK_PERCENT:  return 13;  // MOD (was 48)

        /* Comparison operators - CORRECTED based on baseline IR analysis */
        case TOK_GT:       return 8;   // Greater than (was 4)
        case TOK_GE:       return 6;   // Greater or equal (was 5)
        case TOK_LT:       return 9;   // Less than (was 6, deduced from pattern)
        case TOK_LE:       return 7;   // Less or equal (was 7, correct!)
        case TOK_EQ:       return 4;   // Equal (was 8)
        case TOK_NE:       return 5;   // Not equal (was 9)

        /* Logical operators */
        case TOK_AND:      return 18;  // Logical AND
        case TOK_OR:       return 19;  // Logical OR
        case TOK_NOT:      return 14;  // Logical NOT

        /* Bitwise operators */
        case TOK_BIT_AND:  return 25;  // Bitwise AND (was 20)
        case TOK_BIT_OR:   return 27;  // Bitwise OR (was 21)
        case TOK_BIT_XOR:  return 26;  // Bitwise XOR (was 22)
        case TOK_LSHIFT:   return 29;  // Left shift (was 24)
        case TOK_RSHIFT:   return 30;  // Right shift (was 25)

        default:
            fprintf(stderr, "[ERROR] Unknown token type for subopcode: %d\n", token_type);
            return 0;  // Unknown
    }
}
