#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ael_emit.h"
#include "ir_text_parser.h"

bool ir2ael_convert_program(const IRProgram *program, AelEmitter *out, char *err, size_t err_cap);

