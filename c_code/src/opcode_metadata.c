/*
 * opcode_metadata.c
 * Opcode Metadata Mapping Implementation
 */

#include "opcode_metadata.h"
#include <stddef.h>

/* Opcode metadata table based on baseline IR analysis */
static const OpcodeMetadata opcode_table[] = {
    /* Statement control */
    {0,  3, 7, 1, "STMT_END"},       // Statement end
    {16, 3, 7, 2, "ASSIGN"},         // Assignment
    {17, 6, 4, 1, "EXPR"},           // Expression

    /* Unary operators */
    {14, 3, 9, 1, "NOT"},            // Logical NOT
    {15, 3, 9, 1, "NEG"},            // Unary negation

    /* Binary arithmetic operators */
    {10, 3, 10, 2, "ADD"},           // Addition     (arg3 = opcode itself)
    {11, 3, 11, 2, "SUB"},           // Subtraction  (arg3 = opcode itself)
    {12, 3, 12, 2, "MUL"},           // Multiplication (arg3 = opcode itself)
    {13, 3, 13, 2, "DIV"},           // Division     (arg3 = opcode itself)
    {48, 3, 48, 2, "MOD"},           // Modulo       (arg3 = opcode itself)

    /* Comparison operators */
    {4,  3, 12, 2, "GT"},            // Greater than
    {5,  3, 12, 2, "GE"},            // Greater or equal
    {6,  3, 12, 2, "LT"},            // Less than
    {7,  3, 12, 2, "LE"},            // Less or equal
    {8,  3, 12, 2, "EQ"},            // Equal
    {9,  3, 12, 2, "NE"},            // Not equal

    /* Logical operators */
    {18, 3, 12, 2, "AND"},           // Logical AND
    {19, 3, 12, 2, "OR"},            // Logical OR

    /* Bitwise operators */
    {20, 3, 16, 2, "BIT_AND"},       // Bitwise AND
    {21, 3, 16, 2, "BIT_OR"},        // Bitwise OR
    {22, 3, 16, 2, "BIT_XOR"},       // Bitwise XOR
    {23, 3, 9,  1, "BIT_NOT"},       // Bitwise NOT
    {24, 3, 16, 2, "SHIFT_LEFT"},    // Left shift
    {25, 3, 16, 2, "SHIFT_RIGHT"},   // Right shift

    /* Function call related */
    {48, 3, 16, 0, "CALL"},          // Function call
    {56, 3, 16, 0, "PUSH_ARGS"},     // Push arguments

    /* Control flow */
    {59, 3, 17, 1, "IF_TEST"},       // If condition test
    {60, 3, 9,  1, "POP_BLOCK"},     // Pop block
    {61, 3, 9,  1, "PUSH_BLOCK"},    // Push block

    /* Sentinel */
    {-1, 0, 0, 0, NULL}
};

/* Get metadata for a given sub-opcode */
bool get_opcode_metadata(int16_t subopcode, int16_t *arg2, int16_t *arg3, int16_t *arg4) {
    if (!arg2 || !arg3 || !arg4) {
        return false;
    }

    for (int i = 0; opcode_table[i].subopcode != -1; i++) {
        if (opcode_table[i].subopcode == subopcode) {
            *arg2 = opcode_table[i].arg2;
            *arg3 = opcode_table[i].arg3;
            *arg4 = opcode_table[i].arg4;
            return true;
        }
    }

    // Not found - return defaults
    *arg2 = 0;
    *arg3 = 0;
    *arg4 = 0;
    return false;
}

/* Get operation name */
const char* get_subopcode_name(int16_t subopcode) {
    for (int i = 0; opcode_table[i].subopcode != -1; i++) {
        if (opcode_table[i].subopcode == subopcode) {
            return opcode_table[i].name;
        }
    }
    return "UNKNOWN";
}
