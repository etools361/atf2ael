#pragma once

#include <stdio.h>

/*
 * Debug logging helpers.
 *
 * Default: disabled (no stdout/stderr noise; faster batch runs).
 * Enable by compiling with: /DAEL_DEBUG_LOG=1
 */

#if defined(AEL_DEBUG_LOG) && (AEL_DEBUG_LOG + 0)
#define AEL_DEBUG_PRINTF(...) fprintf(stderr, __VA_ARGS__)
#define AEL_DEBUG_FLUSH() fflush(stderr)
#else
#define AEL_DEBUG_PRINTF(...) ((void)0)
#define AEL_DEBUG_FLUSH() ((void)0)
#endif

