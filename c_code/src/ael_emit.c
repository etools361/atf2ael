#include "ael_emit.h"

#include <string.h>

static bool emit_raw(AelEmitter *e, const char *s, size_t n) {
    if (!e || !e->fp) return false;
    return fwrite(s, 1, n, e->fp) == n;
}

static bool emit_repeat(AelEmitter *e, char ch, int count) {
    if (!e || !e->fp) return false;
    if (count <= 0) return true;

    char buf[4096];
    memset(buf, ch, sizeof(buf));

    int remaining = count;
    while (remaining > 0) {
        int chunk = remaining;
        if (chunk > (int)sizeof(buf)) chunk = (int)sizeof(buf);
        if (!emit_raw(e, buf, (size_t)chunk)) {
            e->last_fail_reason = AEL_EMIT_FAIL_IO;
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

bool ael_emit_init(AelEmitter *e, FILE *fp, bool strict_pos) {
    if (!e || !fp) return false;
    e->fp = fp;
    e->line0 = 0;
    e->col0 = 0;
    e->strict_pos = strict_pos;
    e->allow_num_local_scope_blocks = true;
    e->last_req_line0 = 0;
    e->last_req_col0 = 0;
    e->last_fail_reason = AEL_EMIT_FAIL_NONE;
    return true;
}

bool ael_emit_char(AelEmitter *e, char ch) {
    if (!e || !e->fp) return false;
    if (fputc((unsigned char)ch, e->fp) == EOF) {
        e->last_fail_reason = AEL_EMIT_FAIL_IO;
        return false;
    }
    if (ch == '\n') {
        e->line0++;
        e->col0 = 0;
    } else {
        e->col0++;
    }
    return true;
}

bool ael_emit_text(AelEmitter *e, const char *text) {
    if (!text) return true;
    for (const char *p = text; *p; p++) {
        if (!ael_emit_char(e, *p)) return false;
    }
    return true;
}

bool ael_emit_at(AelEmitter *e, int line0, int col0) {
    if (!e || !e->fp) return false;
    if (!e->strict_pos) return true;
    e->last_req_line0 = line0;
    e->last_req_col0 = col0;
    e->last_fail_reason = AEL_EMIT_FAIL_NONE;

    if (line0 < e->line0) {
        e->last_fail_reason = AEL_EMIT_FAIL_BACKWARD_LINE;
        /* Best-effort: the emitter is a forward-only stream, so we can't
         * physically seek backward. In strict-pos mode we still want the
         * conversion to proceed (even if positions drift) so we treat this as a
         * non-fatal positioning miss.
         */
        return true;
    }
    if (line0 == e->line0 && col0 < e->col0) {
        if (e->strict_pos) {
            e->last_fail_reason = AEL_EMIT_FAIL_BACKWARD_COL;
            /* Best-effort: can't move backward within the current line. */
            return true;
        }
        return true;
    }

    if (e->line0 < line0) {
        int n = line0 - e->line0;
        if (!emit_repeat(e, '\n', n)) return false;
        e->line0 += n;
        e->col0 = 0;
    }
    if (e->col0 < col0) {
        int n = col0 - e->col0;
        if (!emit_repeat(e, ' ', n)) return false;
        e->col0 += n;
    }
    return true;
}
