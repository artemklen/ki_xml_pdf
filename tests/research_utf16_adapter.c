/* SPDX-License-Identifier: MIT */
/* OSR-16 research prototype: an external, incremental UTF-16 -> UTF-8
   read_bytes adapter in front of the unchanged UTF-8 library API. It is not
   part of the library, the CLI or the installed examples. tests/research_utf16.py
   builds and drives it.

   usage: research_utf16_adapter [options] IN OUT
     --mode utf16|utf8   transcode (default) or pass bytes through unchanged
     --src-chunk N       at most N bytes per read(2) of IN (default 4096)
     --out-cap N         at most N bytes per read_bytes return (default: max)
     --cancel-after N    cancelled() returns 1 from the Nth read_bytes call on
     --to-file           ki_convert_to_file(OUT) instead of ki_convert + write
     --font PATH         default fonts/DejaVuSans.ttf
     --dump              write the transcoded UTF-8 stream to OUT, no library
   IN may be "-" for stdin. Prints one JSON object on stdout. */

#define _POSIX_C_SOURCE 200809L

#include "ki_xml_pdf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SRC_BUF 4096
#define STAGE_MAX 256
/* The parser keeps at most a 4096-byte window, so a reported byte offset is
   never more than 4096 UTF-8 bytes (hence scalars) behind what the adapter
   has handed out; one more scalar may be pending. */
#ifndef RING
#define RING 4608
#endif

typedef struct U16 {
    int fd;
    int src_chunk;
    unsigned char in[SRC_BUF];
    int in_len;
    int in_pos;
    int src_eof;
    unsigned long long src_read_total;
    int phase; /* 0 detect, 1 emit staged prefix, 2 body */
    int be;
    int bom_len;
    unsigned long long src_pos; /* source offset of the next unit */
    unsigned char out[4];
    int out_len;
    int out_pos;
    /* staged prefix: up to STAGE_MAX scalars, used for the declaration */
    unsigned char st_utf8[STAGE_MAX * 4];
    unsigned char st_ulen[STAGE_MAX];
    unsigned char st_slen[STAGE_MAX];
    int st_n;
    int st_emit;
    int st_byte;
    /* offset ring of emitted scalars: (utf8 length, source length) */
    unsigned char ring_u[RING];
    unsigned char ring_s[RING];
    unsigned long long ring_n;
    unsigned long long utf8_total;
    unsigned long long src_total;
    int err;
    char errmsg[96];
    unsigned long long err_src;
    int decl_rewritten;
} U16;

static int fail(U16 *a, unsigned long long at, const char *msg) {
    if (!a->err) {
        a->err = 1;
        a->err_src = at;
        snprintf(a->errmsg, sizeof a->errmsg, "%s", msg);
    }
    return -1;
}

/* Ensures k unread source bytes when the source has them. -1 read error. */
static int ensure(U16 *a, int k) {
    while (a->in_len - a->in_pos < k && !a->src_eof) {
        if (a->in_pos > 0) {
            memmove(a->in, a->in + a->in_pos, (size_t)(a->in_len - a->in_pos));
            a->in_len -= a->in_pos;
            a->in_pos = 0;
        }
        int room = SRC_BUF - a->in_len;
        if (room > a->src_chunk) {
            room = a->src_chunk;
        }
        ssize_t r = read(a->fd, a->in + a->in_len, (size_t)room);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail(a, a->src_pos, "source read error");
        }
        if (r == 0) {
            a->src_eof = 1;
        }
        a->in_len += (int)r;
        a->src_read_total += (unsigned long long)r;
    }
    return 0;
}

/* 0 unit read, 1 clean EOF, -1 error. */
static int unit(U16 *a, unsigned *u) {
    if (ensure(a, 2) < 0) {
        return -1;
    }
    int have = a->in_len - a->in_pos;
    if (have == 0) {
        return 1;
    }
    if (have == 1) {
        return fail(a, a->src_pos, "truncated code unit");
    }
    unsigned char *b = a->in + a->in_pos;
    *u = a->be ? ((unsigned)b[0] << 8 | b[1]) : ((unsigned)b[1] << 8 | b[0]);
    a->in_pos += 2;
    a->src_pos += 2;
    return 0;
}

/* 0 scalar, 1 clean EOF, -1 error. Validity as an XML Char is left to the
   library; this checks only UTF-16 well-formedness. */
static int scalar(U16 *a, unsigned *cp, int *slen) {
    unsigned long long at = a->src_pos;
    unsigned u;
    int r = unit(a, &u);
    if (r != 0) {
        return r;
    }
#ifdef LENIENT
    /* Negative control: pass lone surrogates through as 3-byte sequences. */
    if (u >= 0xD800 && u <= 0xDFFF && (u >= 0xDC00 || ensure(a, 2) < 0 || a->in_len - a->in_pos < 2 ||
                                       ((a->be ? a->in[a->in_pos] : a->in[a->in_pos + 1]) & 0xFC) != 0xDC)) {
        *cp = u;
        *slen = 2;
        return 0;
    }
#endif
    if (u >= 0xDC00 && u <= 0xDFFF) {
        return fail(a, at, "unpaired low surrogate");
    }
    if (u < 0xD800 || u > 0xDBFF) {
        *cp = u;
        *slen = 2;
        return 0;
    }
    unsigned v;
    r = unit(a, &v);
    if (r == 1) {
        return fail(a, at, "high surrogate at end of input");
    }
    if (r < 0) {
        return -1;
    }
    if (v < 0xDC00 || v > 0xDFFF) {
        return fail(a, at, "unpaired high surrogate");
    }
    *cp = 0x10000u + ((u - 0xD800u) << 10) + (v - 0xDC00u);
    *slen = 4;
    return 0;
}

static int utf8(unsigned cp, unsigned char *o) {
    if (cp < 0x80) {
        o[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (unsigned char)(0xC0 | cp >> 6);
        o[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (unsigned char)(0xE0 | cp >> 12);
        o[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
        o[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (unsigned char)(0xF0 | cp >> 18);
    o[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3F));
    o[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
    o[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

static void record(U16 *a, int ulen, int slen) {
    a->ring_u[a->ring_n % RING] = (unsigned char)ulen;
    a->ring_s[a->ring_n % RING] = (unsigned char)slen;
    a->ring_n++;
    a->utf8_total += (unsigned long long)ulen;
    a->src_total += (unsigned long long)slen;
}

static int stage_push(U16 *a, unsigned cp, int slen) {
    int n = utf8(cp, a->st_utf8 + a->st_byte);
    a->st_ulen[a->st_n] = (unsigned char)n;
    a->st_slen[a->st_n] = (unsigned char)slen;
    a->st_n++;
    a->st_byte += n;
    return n;
}

static int ascii_ieq(const unsigned char *s, int n, const char *lit) {
    if ((int)strlen(lit) != n) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        int c = s[i];
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
        if (c != lit[i]) {
            return 0;
        }
    }
    return 1;
}

static int is_ws(int c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Policy: a BOM allows a missing encoding or "UTF-16"; without a BOM the
   declaration must name the detected UTF-16LE or UTF-16BE (XML 1.0 4.3.3 and
   appendix F). The value is replaced by UTF-8 and padded with spaces after
   the closing quote, so scalar count, line and column stay the same. */
static int fix_decl(U16 *a, int decl_end) {
    unsigned char *d = a->st_utf8;
    int at = -1;
    for (int i = 5; i + 8 <= decl_end; i++) {
        if (is_ws(d[i - 1]) && memcmp(d + i, "encoding", 8) == 0) {
            at = i + 8;
            break;
        }
    }
    unsigned long long decl_src = (unsigned long long)a->bom_len;
    if (at < 0) {
        if (a->bom_len == 0) {
            return fail(a, decl_src, "no BOM and no UTF-16LE/UTF-16BE declaration");
        }
        return 0;
    }
    while (at < decl_end && is_ws(d[at])) {
        at++;
    }
    if (at >= decl_end || d[at] != '=') {
        return 0; /* malformed: the library reports it */
    }
    at++;
    while (at < decl_end && is_ws(d[at])) {
        at++;
    }
    if (at >= decl_end || (d[at] != '"' && d[at] != '\'')) {
        return 0;
    }
    int q = d[at];
    int v0 = at + 1;
    int v1 = v0;
    while (v1 < decl_end && d[v1] != q) {
        v1++;
    }
    if (v1 >= decl_end) {
        return 0;
    }
    int vn = v1 - v0;
    unsigned long long val_src = (unsigned long long)a->bom_len + 2ull * (unsigned long long)v0;
    int ok;
    if (a->bom_len > 0) {
        ok = ascii_ieq(d + v0, vn, "UTF-16");
    } else {
        ok = ascii_ieq(d + v0, vn, a->be ? "UTF-16BE" : "UTF-16LE");
    }
    if (!ok) {
        return fail(a, val_src, "declared encoding does not match UTF-16 detection");
    }
    memcpy(d + v0, "UTF-8", 5);
    d[v0 + 5] = (unsigned char)q;
    for (int i = v0 + 6; i <= v1; i++) {
        d[i] = ' ';
    }
    a->decl_rewritten = 1;
    return 0;
}

/* Stages "<?xml" + white space ... "?>" (ASCII only) or whatever prefix
   proves there is no declaration. */
static int detect(U16 *a) {
    if (ensure(a, 4) < 0) {
        return -1;
    }
    int have = a->in_len - a->in_pos;
    unsigned char *b = a->in + a->in_pos;
    if (have >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        a->be = 0;
        a->bom_len = 2;
    } else if (have >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        a->be = 1;
        a->bom_len = 2;
    } else if (have >= 4 && b[0] == '<' && b[1] == 0 && b[2] == '?' && b[3] == 0) {
        a->be = 0;
    } else if (have >= 4 && b[0] == 0 && b[1] == '<' && b[2] == 0 && b[3] == '?') {
        a->be = 1;
    } else {
        return fail(a, 0, "not UTF-16: no BOM and no <?xml in UTF-16");
    }
    a->in_pos += a->bom_len;
    a->src_pos = (unsigned long long)a->bom_len;
    a->src_total = (unsigned long long)a->bom_len;
    static const char lead[] = "<?xml";
    int decl_end = -1;
    for (;;) {
        unsigned cp;
        int slen;
        int r = scalar(a, &cp, &slen);
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            break;
        }
        stage_push(a, cp, slen);
        int i = a->st_n - 1;
        if (i < 5) {
            if (cp != (unsigned char)lead[i]) {
                break;
            }
            continue;
        }
        if (i == 5 && !is_ws((int)cp)) {
            break;
        }
        if (cp >= 0x80) {
            return fail(a, a->src_pos - (unsigned)slen, "non-ASCII scalar in XML declaration");
        }
        if (cp == '>' && a->st_utf8[a->st_byte - 2] == '?') {
            decl_end = a->st_byte;
            break;
        }
        if (a->st_n == STAGE_MAX) {
            return fail(a, (unsigned long long)a->bom_len, "XML declaration longer than 255 scalars");
        }
    }
    if (decl_end > 0) {
        if (fix_decl(a, decl_end) < 0) {
            return -1;
        }
    } else if (a->bom_len == 0) {
        return fail(a, 0, "no BOM and no UTF-16LE/UTF-16BE declaration");
    }
    return 0;
}

static int next_out(U16 *a) {
    if (a->phase == 0) {
        if (detect(a) < 0) {
            return -1;
        }
        a->phase = 1;
    }
    if (a->phase == 1) {
        if (a->st_emit < a->st_n) {
            int k = a->st_emit++;
            int off = 0;
            for (int i = 0; i < k; i++) {
                off += a->st_ulen[i];
            }
            memcpy(a->out, a->st_utf8 + off, a->st_ulen[k]);
            a->out_len = a->st_ulen[k];
            a->out_pos = 0;
            record(a, a->st_ulen[k], a->st_slen[k]);
            return 0;
        }
        a->phase = 2;
    }
    unsigned cp;
    int slen;
    int r = scalar(a, &cp, &slen);
    if (r != 0) {
        return r;
    }
    a->out_len = utf8(cp, a->out);
    a->out_pos = 0;
    record(a, a->out_len, slen);
    return 0;
}

typedef struct Ctx {
    U16 *a;
    int mode_utf8;
    int out_cap;
    int cancel_after;
    long reads;
    int max_pending;
    int out_fd;
} Ctx;

static int adapter_read(void *user, char *dst, int max) {
    Ctx *c = user;
    U16 *a = c->a;
    c->reads++;
    int lim = max;
    if (c->out_cap > 0 && lim > c->out_cap) {
        lim = c->out_cap;
    }
    if (c->mode_utf8) {
        ssize_t r;
        do {
            r = read(a->fd, dst, (size_t)lim);
        } while (r < 0 && errno == EINTR);
        return r < 0 ? -1 : (int)r;
    }
    int n = 0;
    while (n < lim) {
        if (a->out_pos < a->out_len) {
            dst[n++] = (char)a->out[a->out_pos++];
            continue;
        }
        if (a->err) {
            break;
        }
        int r = next_out(a);
        if (r != 0) {
            break;
        }
    }
    int pending = a->out_len - a->out_pos;
    if (pending > c->max_pending) {
        c->max_pending = pending;
    }
    if (n > 0) {
        return n;
    }
    return a->err ? -1 : 0;
}

static int adapter_cancel(void *user) {
    Ctx *c = user;
    return c->cancel_after > 0 && c->reads >= c->cancel_after;
}

static int out_write(void *user, const char *src, int n) {
    Ctx *c = user;
    int done = 0;
    while (done < n) {
        ssize_t w = write(c->out_fd, src + done, (size_t)(n - done));
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        done += (int)w;
    }
    return n;
}

/* -1 when B is outside the ring; *mid when B falls inside a scalar. */
static long long map_offset(const U16 *a, unsigned long long b, int *mid) {
    unsigned long long u = a->utf8_total;
    unsigned long long s = a->src_total;
    unsigned long long i = a->ring_n;
    unsigned long long floor = a->ring_n > RING ? a->ring_n - RING : 0;
    *mid = 0;
    while (u > b) {
        if (i == floor) {
            return -1;
        }
        i--;
        u -= a->ring_u[i % RING];
        s -= a->ring_s[i % RING];
    }
    if (u < b) {
        *mid = 1;
    }
    return (long long)s;
}

static void json_str(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') {
            printf("\\%c", ch);
        } else if (ch < 0x20) {
            printf("\\u%04x", ch);
        } else {
            putchar(ch);
        }
    }
    putchar('"');
}

int main(int argc, char **argv) {
    const char *mode = "utf16";
    const char *font = "fonts/DejaVuSans.ttf";
    int src_chunk = SRC_BUF;
    int out_cap = 0;
    int cancel_after = 0;
    int to_file = 0;
    int dump = 0;
    int i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (strcmp(argv[i], "--to-file") == 0) {
            to_file = 1;
        } else if (strcmp(argv[i], "--dump") == 0) {
            dump = 1;
        } else if (i + 1 < argc && strcmp(argv[i], "--mode") == 0) {
            mode = argv[++i];
        } else if (i + 1 < argc && strcmp(argv[i], "--src-chunk") == 0) {
            src_chunk = atoi(argv[++i]);
        } else if (i + 1 < argc && strcmp(argv[i], "--out-cap") == 0) {
            out_cap = atoi(argv[++i]);
        } else if (i + 1 < argc && strcmp(argv[i], "--cancel-after") == 0) {
            cancel_after = atoi(argv[++i]);
        } else if (i + 1 < argc && strcmp(argv[i], "--font") == 0) {
            font = argv[++i];
        } else {
            fprintf(stderr, "bad option %s\n", argv[i]);
            return 2;
        }
    }
    if (argc - i != 2 || src_chunk < 1 || src_chunk > SRC_BUF) {
        fprintf(stderr, "usage: research_utf16_adapter [options] IN OUT\n");
        return 2;
    }
    int fd = strcmp(argv[i], "-") == 0 ? 0 : open(argv[i], O_RDONLY);
    if (fd < 0) {
        perror(argv[i]);
        return 3;
    }
    static U16 a;
    a.fd = fd;
    a.src_chunk = src_chunk;
    Ctx c = {&a, strcmp(mode, "utf8") == 0, out_cap, cancel_after, 0, 0, -1};

    if (dump) {
        c.out_fd = open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (c.out_fd < 0) {
            perror(argv[i + 1]);
            return 3;
        }
        char buf[4096];
        int r;
        while ((r = adapter_read(&c, buf, (int)sizeof buf)) > 0) {
            out_write(&c, buf, r);
        }
        close(c.out_fd);
        printf("{\"dump_status\":%d,\"reads\":%ld,\"utf8_bytes_out\":%llu,\"max_pending\":%d,\"adapter_error\":", r,
               c.reads, a.utf8_total, c.max_pending);
        if (a.err) {
            json_str(a.errmsg);
            printf(",\"adapter_error_src\":%llu}\n", a.err_src);
        } else {
            printf("null}\n");
        }
        return 0;
    }

    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = font;
    opt.read_bytes = adapter_read;
    opt.io_user = &c;
    opt.cancelled = adapter_cancel;
    opt.cancel_user = &c;
    KiResult res;
    memset(&res, 0, sizeof res);
    if (to_file) {
        ki_convert_to_file(argv[i + 1], &opt, &res);
    } else {
        c.out_fd = open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (c.out_fd < 0) {
            perror(argv[i + 1]);
            return 3;
        }
        opt.write_bytes = out_write;
        ki_convert(&opt, &res);
        close(c.out_fd);
    }
    /* The library never closes caller-owned input; the owner does it here. */
    int fd_open_after = fcntl(fd, F_GETFD) != -1;
    if (fd != 0) {
        close(fd);
    }

    unsigned long long line = 0, col = 0, byte = 0;
    int has_pos = 0;
    const char *p = strstr(res.message, " at line ");
    if (p != NULL && sscanf(p, " at line %llu, column %llu, byte %llu", &line, &col, &byte) == 3) {
        has_pos = 1;
    }
    printf("{\"status\":%d,\"cause\":%d,\"message\":", res.status, res.cause);
    json_str(res.message);
    printf(",\"pages\":%d,\"output_bytes\":%ld,\"reads\":%ld,\"fd_open_after\":%d", res.pages, res.output_bytes,
           c.reads, fd_open_after);
    printf(",\"src_bytes_read\":%llu,\"utf8_bytes_out\":%llu,\"max_pending\":%d", a.src_read_total, a.utf8_total,
           c.max_pending);
    printf(",\"state_bytes\":%zu,\"decl_rewritten\":%d", sizeof(U16), a.decl_rewritten);
    printf(",\"adapter_error\":");
    if (a.err) {
        json_str(a.errmsg);
        printf(",\"adapter_error_src\":%llu", a.err_src);
    } else {
        printf("null");
    }
    if (has_pos && !c.mode_utf8) {
        int mid = 0;
        long long src = map_offset(&a, byte, &mid);
        printf(",\"line\":%llu,\"column\":%llu,\"utf8_byte\":%llu,\"src_byte\":%lld,\"mid_scalar\":%d", line, col,
               byte, src, mid);
    } else if (has_pos) {
        printf(",\"line\":%llu,\"column\":%llu,\"utf8_byte\":%llu", line, col, byte);
    }
    printf("}\n");
    return 0;
}
