/*
 * yacc_parser.h
 * YACC Parser interface (stub for new parser compatibility)
 */

#ifndef YACC_PARSER_H
#define YACC_PARSER_H

#include <stdint.h>

/* ========================================
 * YACC Parser Functions
 * ======================================== */

/* Main YACC parser entry (stub - not used in new parser) */
int64_t yacc_parser(void);

/* Initialize YACC parser tables */
void yacc_parser_init_tables(void);

#endif /* YACC_PARSER_H */
