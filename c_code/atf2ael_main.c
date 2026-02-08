/*
 * atf2ael_main.c
 * ATF(.atf) -> IR(.ir.txt) -> AEL(.ael) converter.
 *
 * Notes:
 * - Uses atf2ir_c_code's atf_to_ir() to generate a temporary IR file.
 * - Uses this repo's IR text parser + ir2ael(real) converter to synthesize AEL.
 * - IR position info is debug-only; defaults to non-strict emission.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "ael_emit.h"
#include "ir2ael_convert.h"
#include "ir_text_parser.h"

/* Provided by atf2ir_c_code (linked into this executable). */
int atf_to_ir(const char *atf_file, const char *ir_file);

static void print_usage(const char *exe) {
    fprintf(stderr,
            "ATF to AEL Converter (ATF->IR->AEL)\n"
            "\n"
            "Usage:\n"
            "  %s -In <file.atf> -Out <file.ael> [-EmitIr 0|1] [-OutIr <file.ir.txt>]\n"
            "     [-StrictPos 0|1] [-AllowScopeBlocks 0|1]\n"
            "\n"
            "Notes:\n"
            "  -EmitIr 0: IR is written to a temp file and removed after conversion.\n"
            "  -EmitIr 1: IR is kept (default path: <out>.ir.txt unless -OutIr is given).\n"
            "  -StrictPos defaults to 0 (positions are debug-only).\n"
            "  -AllowScopeBlocks defaults to 0 (do not rely on locals/scope bookkeeping).\n",
            exe);
}

static bool dir_exists(const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool make_parent_dirs(const char *path) {
    char tmp[MAX_PATH * 4];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char ch = *p;
            *p = '\0';
            if (tmp[0] != '\0' && !dir_exists(tmp)) {
                CreateDirectoryA(tmp, NULL);
            }
            *p = ch;
        }
    }
    return true;
}

static void derive_default_ir_path_from_ael(const char *out_ael_path, char *out_ir_path, size_t cap) {
    if (!out_ael_path || !out_ir_path || cap == 0) return;
    size_t n = strlen(out_ael_path);
    if (n >= 4 && _stricmp(out_ael_path + (n - 4), ".ael") == 0) {
        /* replace ".ael" with ".ir.txt" */
        size_t prefix = n - 4;
        if (prefix + 7 + 1 > cap) {
            out_ir_path[0] = '\0';
            return;
        }
        memcpy(out_ir_path, out_ael_path, prefix);
        memcpy(out_ir_path + prefix, ".ir.txt", 7);
        out_ir_path[prefix + 7] = '\0';
        return;
    }
    snprintf(out_ir_path, cap, "%s.ir.txt", out_ael_path);
}

static bool make_temp_ir_file(char *out_path, size_t cap) {
    if (!out_path || cap == 0) return false;
    char tmp_dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    if (n == 0 || n >= sizeof(tmp_dir)) return false;
    char tmp_name[MAX_PATH];
    if (GetTempFileNameA(tmp_dir, "atf2ael", 0, tmp_name) == 0) return false;
    strncpy(out_path, tmp_name, cap - 1);
    out_path[cap - 1] = '\0';
    return true;
}

int main(int argc, char **argv) {
    const char *in_atf = NULL;
    const char *out_ael = NULL;
    const char *out_ir_arg = NULL;
    int emit_ir = -1; /* -1: auto, 0/1: explicit */
    bool strict_pos = false;
    /* Default to disabled: ATF-derived IR may have emitter-dependent locals/scope bookkeeping,
       which should not influence AEL structure unless explicitly requested. */
    bool allow_scope_blocks = false;

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "-In") == 0 && i + 1 < argc) {
            in_atf = argv[++i];
        } else if (_stricmp(argv[i], "-Out") == 0 && i + 1 < argc) {
            out_ael = argv[++i];
        } else if (_stricmp(argv[i], "-OutIr") == 0 && i + 1 < argc) {
            out_ir_arg = argv[++i];
        } else if (_stricmp(argv[i], "-EmitIr") == 0 && i + 1 < argc) {
            emit_ir = atoi(argv[++i]) != 0;
        } else if (_stricmp(argv[i], "-StrictPos") == 0 && i + 1 < argc) {
            strict_pos = atoi(argv[++i]) != 0;
        } else if (_stricmp(argv[i], "-AllowScopeBlocks") == 0 && i + 1 < argc) {
            allow_scope_blocks = atoi(argv[++i]) != 0;
        } else if (_stricmp(argv[i], "-h") == 0 || _stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "/?") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[atf2ael] Unknown arg: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!in_atf || !out_ael) {
        print_usage(argv[0]);
        return 2;
    }

    bool keep_ir = false;
    if (out_ir_arg) {
        keep_ir = true;
    } else if (emit_ir == 1) {
        keep_ir = true;
    } else if (emit_ir == 0) {
        keep_ir = false;
    }

    char ir_path[MAX_PATH * 4];
    ir_path[0] = '\0';
    bool is_temp_ir = false;

    if (keep_ir) {
        if (out_ir_arg) {
            strncpy(ir_path, out_ir_arg, sizeof(ir_path) - 1);
            ir_path[sizeof(ir_path) - 1] = '\0';
        } else {
            derive_default_ir_path_from_ael(out_ael, ir_path, sizeof(ir_path));
        }
        if (!ir_path[0]) {
            fprintf(stderr, "[atf2ael] Failed to derive IR output path.\n");
            return 1;
        }
        make_parent_dirs(ir_path);
    } else {
        if (!make_temp_ir_file(ir_path, sizeof(ir_path))) {
            fprintf(stderr, "[atf2ael] Failed to create temp IR file path.\n");
            return 1;
        }
        is_temp_ir = true;
    }

    make_parent_dirs(out_ael);

    int rc = atf_to_ir(in_atf, ir_path);
    if (rc != 0) {
        fprintf(stderr, "[atf2ael] ATF->IR failed (rc=%d): %s\n", rc, in_atf);
        if (is_temp_ir) DeleteFileA(ir_path);
        return 1;
    }

    char err[512];
    IRProgram program;
    ir_program_init(&program);
    if (!ir_parse_file(ir_path, &program, err, sizeof(err))) {
        fprintf(stderr, "[atf2ael] IR parse failed: %s (%s)\n", ir_path, err);
        ir_program_free(&program);
        if (is_temp_ir) DeleteFileA(ir_path);
        return 1;
    }

    FILE *fp = fopen(out_ael, "wb");
    if (!fp) {
        fprintf(stderr, "[atf2ael] Cannot open output: %s\n", out_ael);
        ir_program_free(&program);
        if (is_temp_ir) DeleteFileA(ir_path);
        return 1;
    }

    AelEmitter emitter;
    ael_emit_init(&emitter, fp, strict_pos);
    emitter.allow_num_local_scope_blocks = allow_scope_blocks;

    bool ok = ir2ael_convert_program(&program, &emitter, err, sizeof(err));
    fclose(fp);
    ir_program_free(&program);

    if (!ok) {
        fprintf(stderr, "[atf2ael] Convert failed: %s\n", err);
        if (is_temp_ir) DeleteFileA(ir_path);
        return 1;
    }

    if (is_temp_ir) {
        DeleteFileA(ir_path);
    } else {
        fprintf(stderr, "[atf2ael] IR output: %s\n", ir_path);
    }

    return 0;
}
