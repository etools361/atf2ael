#include "ir_text_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ir_inst_free(IRInst *inst) {
    if (!inst) return;
    free(inst->str);
    memset(inst, 0, sizeof(*inst));
    inst->index = -1;
}

bool ir_program_init(IRProgram *p) {
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    return true;
}

void ir_program_free(IRProgram *p) {
    if (!p) return;
    for (size_t i = 0; i < p->count; i++) ir_inst_free(&p->insts[i]);
    free(p->insts);
    memset(p, 0, sizeof(*p));
}

static bool ensure_cap(IRProgram *p, size_t want) {
    if (want <= p->cap) return true;
    size_t new_cap = (p->cap == 0) ? 128 : (p->cap * 2);
    while (new_cap < want) new_cap *= 2;
    IRInst *new_insts = (IRInst *)realloc(p->insts, new_cap * sizeof(IRInst));
    if (!new_insts) return false;
    for (size_t i = p->cap; i < new_cap; i++) {
        memset(&new_insts[i], 0, sizeof(IRInst));
        new_insts[i].index = -1;
    }
    p->insts = new_insts;
    p->cap = new_cap;
    return true;
}

static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || isspace((unsigned char)s[n - 1]))) {
        s[n - 1] = '\0';
        n--;
    }
}

static bool parse_int_at(const char **ps, int *out_val) {
    const char *s = skip_ws(*ps);
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (!isdigit((unsigned char)*s)) return false;
    long v = 0;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    if (neg) v = -v;
    *out_val = (int)v;
    *ps = s;
    return true;
}

static bool parse_double_at(const char **ps, double *out_val) {
    const char *s = skip_ws(*ps);
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) return false;
    *out_val = v;
    *ps = end;
    return true;
}

static bool parse_quoted_string(const char **ps, char **out) {
    const char *s = skip_ws(*ps);
    if (*s != '"') return false;
    s++;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    /*
     * Hooked/baseline IR logs typically print string payloads without C-style unescaping.
     * We must still recognize the closing quote. Use the standard rule:
     *   a quote terminates the string unless preceded by an odd run of backslashes.
     * IMPORTANT: we preserve bytes as-is (including backslashes), so IR->AEL can re-emit
     * literals that keep sequences like \" and \\ intact.
     */
    int backslash_run = 0;
    while (*s) {
        char c = *s;
        if (c == '"') {
            if ((backslash_run % 2) == 0) break; /* terminator */
            /* escaped quote: keep it */
            if (len + 1 >= cap) {
                cap *= 2;
                char *nb = (char *)realloc(buf, cap);
                if (!nb) {
                    free(buf);
                    return false;
                }
                buf = nb;
            }
            buf[len++] = '"';
            s++;
            backslash_run = 0;
            continue;
        }
        s++;
        if (c == '\\') backslash_run++;
        else backslash_run = 0;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                return false;
            }
            buf = nb;
        }
        buf[len++] = c;
    }
    if (*s != '"') {
        free(buf);
        return false;
    }
    s++; /* closing quote */
    buf[len] = '\0';
    *out = buf;
    *ps = s;
    return true;
}

static bool parse_key_int(const char *key, const char **ps, bool *has, int *val) {
    const char *s = skip_ws(*ps);
    size_t klen = strlen(key);
    if (strncmp(s, key, klen) != 0) return false;
    s += klen;
    s = skip_ws(s);
    if (*s != '=') return false;
    s++;
    if (!parse_int_at(&s, val)) return false;
    *has = true;
    *ps = s;
    return true;
}

static bool parse_key_double(const char *key, const char **ps, bool *has, double *val) {
    const char *s = skip_ws(*ps);
    size_t klen = strlen(key);
    if (strncmp(s, key, klen) != 0) return false;
    s += klen;
    s = skip_ws(s);
    if (*s != '=') return false;
    s++;
    if (!parse_double_at(&s, val)) return false;
    *has = true;
    *ps = s;
    return true;
}

static bool parse_key_str(const char *key, const char **ps, char **out) {
    const char *s = skip_ws(*ps);
    size_t klen = strlen(key);
    if (strncmp(s, key, klen) != 0) return false;
    s += klen;
    s = skip_ws(s);
    if (*s != '=') return false;
    s++;
    s = skip_ws(s);
    if (!parse_quoted_string(&s, out)) return false;
    *ps = s;
    return true;
}

static bool parse_ir_line(const char *line_in, IRInst *out_inst) {
    char line[2048];
    strncpy(line, line_in, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    rstrip(line);
    const char *s = skip_ws(line);

    if (*s == '\0') return false;
    if (*s == '#') return false;
    if (*s == '[') {
        /* [000A] ... */
        s++;
        while (*s && *s != ']') s++;
        if (*s == ']') s++;
    }
    s = skip_ws(s);
    if (strncmp(s, "OP", 2) != 0) return false;
    bool has_op = false;
    if (!parse_key_int("OP", &s, &has_op, &out_inst->op)) return false;
    if (!has_op) return false;

    out_inst->index = -1;
    out_inst->has_arg1 = out_inst->has_arg2 = out_inst->has_arg3 = out_inst->has_a4 = false;
    out_inst->arg1 = out_inst->arg2 = out_inst->arg3 = out_inst->a4 = 0;
    out_inst->str = NULL;
    out_inst->has_num_val = false;
    out_inst->num_val = 0.0;

    while (*s) {
        s = skip_ws(s);
        if (*s == '\0') break;
        if (*s == '#') break;

        if (parse_key_int("arg1", &s, &out_inst->has_arg1, &out_inst->arg1)) continue;
        if (parse_key_int("arg2", &s, &out_inst->has_arg2, &out_inst->arg2)) continue;
        if (parse_key_int("arg3", &s, &out_inst->has_arg3, &out_inst->arg3)) continue;
        if (parse_key_int("a4", &s, &out_inst->has_a4, &out_inst->a4)) continue;
        if (!out_inst->str && parse_key_str("str", &s, &out_inst->str)) continue;
        if (!out_inst->has_num_val && parse_key_double("real", &s, &out_inst->has_num_val, &out_inst->num_val)) continue;
        if (!out_inst->has_num_val && parse_key_double("imag", &s, &out_inst->has_num_val, &out_inst->num_val)) continue;

        /* Skip unknown token */
        while (*s && !isspace((unsigned char)*s)) s++;
    }

    /*
     * Some hooked IR logs appear to store line numbers as a signed 16-bit value (wrap at 65536),
     * which makes large-line stress cases show up as negative arg2. In those cases, treat arg2
     * as an unsigned 16-bit line index to recover the original positive line number.
     */
    if (out_inst->has_arg2 && out_inst->arg2 < 0 && out_inst->arg2 >= -32768) {
        out_inst->arg2 += 65536;
    }

    /* Parse numeric payload from inline comments for LOAD_REAL / LOAD_IMAG. */
    if (out_inst->op == 8 || out_inst->op == 9) {
        const char *tag = (out_inst->op == 8) ? "LOAD_REAL val=" : "LOAD_IMAG val=";
        const char *p = strstr(line_in, tag);
        if (p) {
            p += strlen(tag);
            char *end = NULL;
            double v = strtod(p, &end);
            if (end != p) {
                out_inst->has_num_val = true;
                out_inst->num_val = v;
            }
        }
    }
    return true;
}

bool ir_parse_file(const char *path, IRProgram *out_program, char *err, size_t err_cap) {
    if (!path || !out_program) return false;
    if (err && err_cap) err[0] = '\0';

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (err && err_cap) snprintf(err, err_cap, "cannot open: %s", path);
        return false;
    }

    IRProgram tmp;
    ir_program_init(&tmp);

    int current_depth = 0;
    long last_inst_index = -1;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        const char *s = skip_ws(line);
        if (*s == '\0') continue;
        if (*s == '\r' || *s == '\n') continue;
        if (*s == '/' && s[1] == '/') continue;
        {
            const char *dp = strstr(s, "DEPTH=");
            if (dp) {
                dp += 6;
                int d = 0;
                const char *t = dp;
                if (parse_int_at(&t, &d)) {
                    current_depth = d;
                    /* Hooked IR logs print DEPTH on the indented line *after* the instruction it describes. */
                    if (last_inst_index >= 0 && (size_t)last_inst_index < tmp.count) {
                        tmp.insts[(size_t)last_inst_index].has_depth = true;
                        tmp.insts[(size_t)last_inst_index].depth = d;
                    }
                }
                continue;
            }
        }

        if (*s == '#' || (s[0] == '/' && s[1] == '*')) continue;
        {
            /* (legacy) treat indented # lines as comments */
            if (s[0] == '#' || (s[0] == ' ' && s[1] == '#')) continue;
        }

        IRInst inst;
        memset(&inst, 0, sizeof(inst));
        inst.index = -1;
        if (!parse_ir_line(s, &inst)) continue;
        inst.has_depth = true;
        inst.depth = current_depth;
        if (!ensure_cap(&tmp, tmp.count + 1)) {
            ir_inst_free(&inst);
            fclose(fp);
            ir_program_free(&tmp);
            if (err && err_cap) snprintf(err, err_cap, "out of memory");
            return false;
        }
        tmp.insts[tmp.count++] = inst;
        last_inst_index = (long)tmp.count - 1;
    }

    fclose(fp);
    *out_program = tmp;
    return true;
}

static void trim_in_place(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    rstrip(s);
}

bool ir_extract_source_ael_path(const char *ir_path, char *out_path, size_t out_cap) {
    if (!ir_path || !out_path || out_cap == 0) return false;
    out_path[0] = '\0';

    FILE *fp = fopen(ir_path, "rb");
    if (!fp) return false;

    char line[2048];
    for (int i = 0; i < 50 && fgets(line, sizeof(line), fp); i++) {
        if (strncmp(line, "# Source:", 9) == 0) {
            char *p = line + 9;
            trim_in_place(p);
            strncpy(out_path, p, out_cap - 1);
            out_path[out_cap - 1] = '\0';
            fclose(fp);
            return out_path[0] != '\0';
        }
    }

    fclose(fp);
    return false;
}
