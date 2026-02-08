/*
 * token_to_subopcode.h
 * Token to IR Subopcode Mapping
 */

#ifndef TOKEN_TO_SUBOPCODE_H
#define TOKEN_TO_SUBOPCODE_H

#include <stdint.h>

/* Convert lexer token type to OP=48 sub-opcode */
int16_t token_to_subopcode(int token_type);

#endif /* TOKEN_TO_SUBOPCODE_H */
