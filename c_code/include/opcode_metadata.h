/*
 * opcode_metadata.h
 * Opcode Metadata Mapping - Provides correct arg2/arg3/arg4 values for OP=48 sub-opcodes
 */

#ifndef OPCODE_METADATA_H
#define OPCODE_METADATA_H

#include <stdint.h>
#include <stdbool.h>

/* Opcode metadata structure */
typedef struct {
    int16_t subopcode;     // OP=48's arg1 sub-opcode
    int16_t arg2;          // Type identifier (3=SM_VALUE, 4=SM_STACK, ...)
    int16_t arg3;          // Stack opcode
    int16_t arg4;          // Number of arguments
    const char *name;      // Operation name
} OpcodeMetadata;

/* Get metadata for a given sub-opcode */
bool get_opcode_metadata(int16_t subopcode, int16_t *arg2, int16_t *arg3, int16_t *arg4);

/* Get operation name */
const char* get_subopcode_name(int16_t subopcode);

#endif /* OPCODE_METADATA_H */
