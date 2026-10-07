#define _POSIX_C_SOURCE 200809L

#include "xml_parse.h"

#include <stdio.h>
#include <string.h>

static int fill(XmlParser *p) {
    if (p->pos > 0) {
        size_t rest = p->len - p->pos;
        memmove(p->buf, p->buf + p->pos, rest);
        p->len = rest;
        p->pos = 0;
    }
    if (p->eof || p->len >= XML_BUF_SIZE) {
        return 0;
    }
    int n = p->read(p->user, p->buf + p->len, (int)(XML_BUF_SIZE - p->len));
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        p->eof = 1;
        return 0;
    }
    p->len += (size_t)n;
    return n;
}

/* >=0 byte, -1 read error, -2 EOF. */
static int peek(XmlParser *p) {
    if (p->pos >= p->len) {
        int n = fill(p);
        if (n < 0) {
            return -1;
        }
        if (p->pos >= p->len) {
            return -2;
        }
    }
    return (unsigned char)p->buf[p->pos];
}

static int feed_utf8(XmlParser *p, unsigned char c) {
    if (p->utf8_need == 0) {
        if (c == 0) {
            return -1;
        }
        if (c < 0x80) {
            return 0;
        }
        if ((c & 0xE0) == 0xC0) {
            if (c < 0xC2) {
                return -1;
            }
            p->utf8_need = 1;
            p->utf8_lead = c;
            return 0;
        }
        if ((c & 0xF0) == 0xE0) {
            p->utf8_need = 2;
            p->utf8_lead = c;
            return 0;
        }
        if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4) {
                return -1;
            }
            p->utf8_need = 3;
            p->utf8_lead = c;
            return 0;
        }
        return -1;
    }
    if ((c & 0xC0) != 0x80) {
        return -1;
    }
    if (p->utf8_lead == 0xE0 && p->utf8_need == 2 && c < 0xA0) {
        return -1;
    }
    if (p->utf8_lead == 0xED && p->utf8_need == 2 && c > 0x9F) {
        return -1;
    }
    if (p->utf8_lead == 0xF0 && p->utf8_need == 3 && c < 0x90) {
        return -1;
    }
    if (p->utf8_lead == 0xF4 && p->utf8_need == 3 && c > 0x8F) {
        return -1;
    }
    p->utf8_need--;
    if (p->utf8_need == 0) {
        p->utf8_lead = 0;
    }
    return 0;
}

static int xml_cp_ok(unsigned cp);

static int nextch(XmlParser *p) {
    int c = peek(p);
    if (c >= 0) {
        p->pos++;
        p->offset++;
        if (feed_utf8(p, (unsigned char)c) != 0) {
            p->utf8_bad = 1;
        } else if (c < 0x80 && !xml_cp_ok((unsigned)c)) {
            p->utf8_bad = 1;
        }
    }
    return c;
}

static int fail(XmlParser *p, XmlEvent *ev, const char *msg);

static int fail_at(XmlParser *p, XmlEvent *ev, const char *msg) {
    char buf[160];
    snprintf(buf, sizeof buf, "%s at byte %zu", msg, p->offset);
    return fail(p, ev, buf);
}

static int note_bytes(XmlParser *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (feed_utf8(p, (unsigned char)p->buf[p->pos + i]) != 0) {
            p->utf8_bad = 1;
        }
    }
    p->pos += n;
    p->offset += n;
    return 0;
}

static int fail(XmlParser *p, XmlEvent *ev, const char *msg) {
    p->failed = 1;
    snprintf(p->errmsg, sizeof p->errmsg, "%s", msg);
    ev->kind = XML_EV_ERROR;
    ev->byte_offset = p->offset;
    snprintf(ev->error, sizeof ev->error, "%s", msg);
    return XML_EV_ERROR;
}

static int is_name_start(int c) {
    if (c >= 128) {
        return 1;
    }
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
}

static int is_name_char(int c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static int is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int skip_ws(XmlParser *p) {
    int n = 0;
    for (;;) {
        int c = peek(p);
        if (c < 0) {
            return c;
        }
        if (!is_space(c)) {
            return n;
        }
        nextch(p);
        n++;
    }
}

static int read_name(XmlParser *p, char *out, int cap) {
    int c = peek(p);
    if (!is_name_start(c)) {
        return -1;
    }
    int n = 0;
    while (is_name_char(c)) {
        if (n + 1 >= cap) {
            return -1;
        }
        if (nextch(p) < 0) {
            return -1;
        }
        out[n++] = (char)c;
        c = peek(p);
        if (c == -1) {
            return -1;
        }
        if (c < 0) {
            break;
        }
    }
    out[n] = '\0';
    return n;
}

static int xml_cp_ok(unsigned cp) {
    return cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
           (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
}

static int utf8_encode(unsigned cp, char out[4]) {
    if (cp <= 0x7Fu) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FFu) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp <= 0xFFFFu) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

static int hex_val(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* pos sits on '&' and the next byte is '#'. Consumes the reference.
   >0 = UTF-8 bytes written, -1 = read error, -2 = illegal reference. */
static int try_numeric(XmlParser *p, char decoded[4]) {
    if (nextch(p) != '&' || nextch(p) != '#') {
        return -2;
    }
    int c = peek(p);
    if (c == -1) {
        return -1;
    }
    if (c < 0) {
        return -2;
    }
    int hex = 0;
    if (c == 'x' || c == 'X') {
        hex = 1;
        if (nextch(p) < 0) {
            return -1;
        }
    }
    unsigned value = 0;
    int digits = 0;
    for (;;) {
        c = peek(p);
        if (c == -1) {
            return -1;
        }
        if (c < 0) {
            return -2;
        }
        if (c == ';') {
            nextch(p);
            break;
        }
        int d = hex ? hex_val(c) : (c >= '0' && c <= '9' ? c - '0' : -1);
        if (d < 0) {
            return -2;
        }
        nextch(p);
        digits++;
        if (hex) {
            if (value > (0x10FFFFu >> 4)) {
                return -2;
            }
            value = (value << 4) + (unsigned)d;
            if (value > 0x10FFFFu) {
                return -2;
            }
        } else {
            if (value > 0x10FFFFu / 10u) {
                return -2;
            }
            unsigned nv = value * 10u + (unsigned)d;
            if (nv > 0x10FFFFu) {
                return -2;
            }
            value = nv;
        }
    }
    if (digits == 0 || !xml_cp_ok(value)) {
        return -2;
    }
    return utf8_encode(value, decoded);
}

/* Predefined names only. pos sits on '&'. 1 = consumed, 0 = not one
   (pos unchanged), -1 = read error. */
static int try_named(XmlParser *p, char decoded[4]) {
    for (;;) {
        size_t avail = p->len - p->pos;
        size_t scan = avail > 6 ? 6 : avail;
        int found_semi = 0;
        size_t semi_at = 0;
        int invalid = 0;
        for (size_t i = 1; i < scan; i++) {
            unsigned char c = (unsigned char)p->buf[p->pos + i];
            if (c == ';') {
                found_semi = 1;
                semi_at = i;
                break;
            }
            int letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!letter) {
                invalid = 1;
                break;
            }
        }
        if (invalid) {
            return 0;
        }
        if (found_semi) {
            const char *b = p->buf + p->pos + 1;
            size_t n = semi_at;
            if (n == 4 && memcmp(b, "amp;", 4) == 0) {
                *decoded = '&';
                note_bytes(p, 5);
                return 1;
            }
            if (n == 3 && memcmp(b, "lt;", 3) == 0) {
                *decoded = '<';
                note_bytes(p, 4);
                return 1;
            }
            if (n == 3 && memcmp(b, "gt;", 3) == 0) {
                *decoded = '>';
                note_bytes(p, 4);
                return 1;
            }
            if (n == 5 && memcmp(b, "quot;", 5) == 0) {
                *decoded = '"';
                note_bytes(p, 6);
                return 1;
            }
            if (n == 5 && memcmp(b, "apos;", 5) == 0) {
                *decoded = '\'';
                note_bytes(p, 6);
                return 1;
            }
            return -3;
        }
        if (p->eof || scan >= 6) {
            return 0;
        }
        size_t before = p->len - p->pos;
        if (fill(p) < 0) {
            return -1;
        }
        if (p->len - p->pos <= before) {
            return 0;
        }
    }
}

/* pos sits on '&'. >0 = bytes written to decoded, 0 = not an entity
   (pos unchanged), -1 = read error, -2 = illegal numeric reference. */
static int try_entity(XmlParser *p, char decoded[4]) {
    for (;;) {
        size_t avail = p->len - p->pos;
        if (avail >= 2 || p->eof) {
            break;
        }
        size_t before = avail;
        if (fill(p) < 0) {
            return -1;
        }
        if (p->len - p->pos <= before) {
            return -3;
        }
    }
    if (p->len - p->pos >= 2 && (unsigned char)p->buf[p->pos + 1] == '#') {
        return try_numeric(p, decoded);
    }
    int named = try_named(p, decoded);
    if (named == 0) {
        return -3;
    }
    return named;
}

static int read_quoted(XmlParser *p, char *out, int cap) {
    int q = nextch(p);
    if (q != '"' && q != '\'') {
        return -1;
    }
    int n = 0;
    for (;;) {
        int c = peek(p);
        if (c < 0) {
            return -1;
        }
        if (c == q) {
            nextch(p);
            out[n] = '\0';
            return n;
        }
        if (c == '&') {
            char d[4];
            int r = try_entity(p, d);
            if (r == -2 || r == -3) {
                return r;
            }
            if (r < 0) {
                return -1;
            }
            if (r > 0) {
                if (n + r >= cap) {
                    return -1;
                }
                memcpy(out + n, d, (size_t)r);
                n += r;
                continue;
            }
        }
        if (c == '<') {
            return -4;
        }
        c = nextch(p);
        if (c < 0) {
            return -1;
        }
        if (c == '\r') {
            if (peek(p) == '\n' && nextch(p) < 0) {
                return -1;
            }
            c = ' ';
        } else if (c == '\n' || c == '\t') {
            c = ' ';
        }
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = (char)c;
    }
}

static int push_name(XmlParser *p, const char *name) {
    if (p->depth >= XML_MAX_DEPTH) {
        return -1;
    }
    snprintf(p->stack[p->depth], XML_NAME_MAX, "%s", name);
    p->depth++;
    return 0;
}

static int pop_name(XmlParser *p, const char *name) {
    if (p->depth <= 0) {
        return -1;
    }
    p->depth--;
    if (strcmp(p->stack[p->depth], name) != 0) {
        return -2;
    }
    return 0;
}

/* 1 = the next bytes are exactly mark, 0 = they are not, -1 = read error.
   Does not consume. */
static int match_at(XmlParser *p, const char *mark, size_t mlen) {
    for (;;) {
        size_t avail = p->len - p->pos;
        if (avail >= mlen) {
            return memcmp(p->buf + p->pos, mark, mlen) == 0;
        }
        if (avail > 0 && memcmp(p->buf + p->pos, mark, avail) != 0) {
            return 0;
        }
        if (p->eof) {
            return 0;
        }
        size_t before = p->len - p->pos;
        if (fill(p) < 0) {
            return -1;
        }
        if (p->len - p->pos <= before) {
            return 0;
        }
    }
}

static int consume_n(XmlParser *p, int n) {
    for (int i = 0; i < n; i++) {
        if (nextch(p) < 0) {
            return -1;
        }
    }
    return 0;
}

/* cdata_mark is how many ']' are held back. A third ']' is emitted, because
   only the last two can still begin "]]>". */
static int skip_comment(XmlParser *p) {
    int dashes = 0;
    for (;;) {
        int c = nextch(p);
        if (c < 0) {
            return -1;
        }
        if (p->utf8_bad) {
            return -2;
        }
        if (c == '-') {
            dashes++;
            if (dashes > 2) {
                return -2;
            }
            continue;
        }
        if (dashes == 2) {
            if (c == '>') {
                return 0;
            }
            return -2;
        }
        dashes = 0;
    }
}

static int parse_cdata(XmlParser *p, XmlEvent *ev) {
    size_t n = 0;
    int ended = 0;
    while (n + 1 < XML_TEXT_MAX) {
        int c = peek(p);
        if (c == -1) {
            return fail(p, ev, "read error");
        }
        if (c == -2) {
            return fail(p, ev, "truncated cdata");
        }
        if (c == ']') {
            if (nextch(p) < 0) {
                return fail(p, ev, "read error");
            }
            if (p->cdata_mark < 2) {
                p->cdata_mark++;
                continue;
            }
            if (p->utf8_bad) {
                return fail_at(p, ev, "illegal character");
            }
            ev->text[n++] = ']';
            continue;
        }
        if (c == '>' && p->cdata_mark == 2) {
            if (nextch(p) < 0) {
                return fail(p, ev, "read error");
            }
            p->in_cdata = 0;
            p->cdata_mark = 0;
            ended = 1;
            break;
        }
        if (p->cdata_mark > 0) {
            ev->text[n++] = ']';
            p->cdata_mark--;
            continue;
        }
        if (nextch(p) < 0) {
            return fail(p, ev, "read error");
        }
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (c == '\r') {
            if (peek(p) == '\n' && nextch(p) < 0) {
                return fail(p, ev, "read error");
            }
            c = '\n';
        }
        ev->text[n++] = (char)c;
    }
    if (n == 0 && ended) {
        ev->text[0] = '\0';
        ev->text_len = 0;
        ev->kind = XML_EV_NONE;
        return XML_EV_NONE;
    }
    if (n == 0) {
        return fail(p, ev, "internal cdata");
    }
    ev->text[n] = '\0';
    ev->text_len = n;
    ev->from_cdata = 1;
    ev->kind = XML_EV_TEXT;
    ev->byte_offset = p->offset;
    return XML_EV_TEXT;
}

/* '!' has been consumed. Comments and a leading DOCTYPE are skipped.
   External identifiers are not fetched. Anything else is an error. */
static int skip_decl_rest(XmlParser *p) {
    int quote = 0;
    int depth = 0;
    for (;;) {
        int c = nextch(p);
        if (c < 0) {
            return -1;
        }
        if (quote) {
            if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '[') {
            depth++;
            continue;
        }
        if (c == ']' && depth > 0) {
            depth--;
            continue;
        }
        if (c == '>' && depth == 0) {
            return 0;
        }
    }
}

static int skip_bang(XmlParser *p, XmlEvent *ev) {
    int c1 = peek(p);
    if (c1 < 0) {
        return fail(p, ev, "truncated declaration");
    }
    if (c1 == '-') {
        if (nextch(p) < 0 || nextch(p) != '-') {
            return fail_at(p, ev, "bad comment");
        }
        int rc = skip_comment(p);
        if (rc == -2) {
            return fail_at(p, ev, "bad comment");
        }
        if (rc < 0) {
            return fail(p, ev, "truncated comment");
        }
        return XML_EV_NONE;
    }
    int is_doc = match_at(p, "DOCTYPE", 7);
    if (is_doc < 0) {
        return fail(p, ev, "read error");
    }
    if (is_doc == 1) {
        if (p->depth != 0 || p->seen_root) {
            return fail_at(p, ev, "doctype placement");
        }
        if (consume_n(p, 7) < 0 || skip_decl_rest(p) < 0) {
            return fail(p, ev, "truncated declaration");
        }
        return XML_EV_NONE;
    }
    return fail_at(p, ev, "unsupported declaration");
}

static int note_char_data(XmlParser *p, XmlEvent *ev, int c) {
    if (c == ']') {
        if (p->bracket_run < 2) {
            p->bracket_run++;
        }
        return 0;
    }
    if (c == '>' && p->bracket_run == 2) {
        return fail_at(p, ev, "']]>' in text");
    }
    p->bracket_run = 0;
    return 0;
}

static int parse_text(XmlParser *p, XmlEvent *ev) {
    p->document_start = 0;
    size_t n = 0;
    while (n + 1 < XML_TEXT_MAX) {
        int c = peek(p);
        if (c == -1) {
            return fail(p, ev, "read error");
        }
        if (c == -2 || c == '<') {
            break;
        }
        if (c == '&') {
            /* Leave the reference for the next chunk when this one cannot
               hold a 4-byte UTF-8 sequence plus its NUL. */
            if (n > 0 && n + 4 >= XML_TEXT_MAX) {
                break;
            }
            char d[4];
            int r = try_entity(p, d);
            if (r == -2) {
                return fail_at(p, ev, "bad character reference");
            }
            if (r == -3 || r == 0) {
                return fail_at(p, ev, "unknown entity");
            }
            if (r < 0) {
                return fail_at(p, ev, "read error");
            }
            if (p->depth == 0) {
                for (int i = 0; i < r; i++) {
                    if (!is_space((unsigned char)d[i])) {
                        return fail_at(p, ev, "text outside document");
                    }
                }
            }
            if (r > 0) {
                if (n + (size_t)r >= XML_TEXT_MAX) {
                    return fail(p, ev, "internal text");
                }
                for (int i = 0; i < r; i++) {
                    if (note_char_data(p, ev, (unsigned char)d[i]) != 0) {
                        return XML_EV_ERROR;
                    }
                }
                memcpy(ev->text + n, d, (size_t)r);
                n += (size_t)r;
                continue;
            }
        }
        c = nextch(p);
        if (c < 0) {
            return fail(p, ev, "read error");
        }
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (c == '\r') {
            if (peek(p) == '\n' && nextch(p) < 0) {
                return fail(p, ev, "read error");
            }
            c = '\n';
        }
        if (p->depth == 0 && !is_space(c)) {
            return fail_at(p, ev, "text outside document");
        }
        if (note_char_data(p, ev, c) != 0) {
            return XML_EV_ERROR;
        }
        ev->text[n++] = (char)c;
    }
    ev->text[n] = '\0';
    ev->text_len = n;
    ev->kind = XML_EV_TEXT;
    ev->byte_offset = p->offset;
    return XML_EV_TEXT;
}

static int parse_end(XmlParser *p, XmlEvent *ev) {
    if (nextch(p) != '/') {
        return fail(p, ev, "bad end tag");
    }
    char name[XML_NAME_MAX];
    if (read_name(p, name, (int)sizeof name) <= 0) {
        return fail(p, ev, "bad end tag");
    }
    int sw = skip_ws(p);
    if (sw < 0) {
        return fail(p, ev, "truncated end tag");
    }
    if (nextch(p) != '>') {
        return fail(p, ev, "bad end tag");
    }
    int pr = pop_name(p, name);
    if (pr == -1) {
        return fail(p, ev, "end tag without start");
    }
    if (pr == -2) {
        return fail(p, ev, "end tag mismatch");
    }
    snprintf(ev->name, sizeof ev->name, "%s", name);
    ev->kind = XML_EV_END;
    ev->byte_offset = p->offset;
    return XML_EV_END;
}

static int parse_start(XmlParser *p, XmlEvent *ev) {
    char name[XML_NAME_MAX];
    if (read_name(p, name, (int)sizeof name) <= 0) {
        return fail(p, ev, "bad start tag");
    }
    ev->nattr = 0;
    for (;;) {
        int sw = skip_ws(p);
        if (sw < 0) {
            return fail(p, ev, "truncated start tag");
        }
        int c = peek(p);
        if (c < 0) {
            return fail(p, ev, "truncated start tag");
        }
        if (c == '>' || c == '/') {
            break;
        }
        if (ev->nattr > 0 && sw == 0) {
            return fail_at(p, ev, "missing space between attributes");
        }
        if (ev->nattr >= XML_ATTR_MAX) {
            return fail(p, ev, "too many attributes");
        }
        int ai = ev->nattr;
        if (read_name(p, ev->attr_name[ai], XML_NAME_MAX) <= 0) {
            return fail(p, ev, "bad attribute");
        }
        for (int j = 0; j < ai; j++) {
            if (strcmp(ev->attr_name[j], ev->attr_name[ai]) == 0) {
                return fail_at(p, ev, "duplicate attribute");
            }
        }
        if (skip_ws(p) < 0) {
            return fail(p, ev, "truncated attribute");
        }
        if (nextch(p) != '=') {
            return fail(p, ev, "attribute without value");
        }
        if (skip_ws(p) < 0) {
            return fail(p, ev, "truncated attribute");
        }
        int qn = read_quoted(p, ev->attr_val[ai], XML_ATTR_VAL_MAX);
        if (qn == -2) {
            return fail_at(p, ev, "bad character reference");
        }
        if (qn == -3) {
            return fail_at(p, ev, "unknown entity");
        }
        if (qn == -4) {
            return fail_at(p, ev, "raw '<' in attribute");
        }
        if (qn < 0) {
            return fail(p, ev, "bad attribute value");
        }
        ev->nattr++;
    }
    int c = nextch(p);
    int self = 0;
    if (c == '/') {
        self = 1;
        if (nextch(p) != '>') {
            return fail(p, ev, "bad empty element");
        }
    } else if (c != '>') {
        return fail(p, ev, "bad start tag");
    }
    if (p->depth == 0) {
        if (p->seen_root) {
            return fail_at(p, ev, "multiple document elements");
        }
        p->seen_root = 1;
    }
    if (p->utf8_bad) {
        return fail_at(p, ev, "illegal character");
    }
    if (push_name(p, name) != 0) {
        return fail(p, ev, "element nesting too deep");
    }
    snprintf(ev->name, sizeof ev->name, "%s", name);
    ev->kind = XML_EV_START;
    ev->byte_offset = p->offset;
    if (self) {
        p->pending_end = 1;
        snprintf(p->end_name, sizeof p->end_name, "%s", name);
    }
    return XML_EV_START;
}

static int eq_token(const char *s, size_t n, const char *lit) {
    size_t m = strlen(lit);
    return n == m && memcmp(s, lit, m) == 0;
}

/* body includes the final '?'. 0 = acceptable XML declaration. */
static int xml_decl_ok(const char *body) {
    const char *s = body;
    if (strncmp(s, "xml", 3) != 0) {
        return -1;
    }
    s += 3;
    if (!is_space((unsigned char)*s)) {
        return -1;
    }
    int saw_version = 0;
    while (*s != '\0' && *s != '?') {
        while (is_space((unsigned char)*s)) {
            s++;
        }
        if (*s == '?' || *s == '\0') {
            break;
        }
        const char *key = s;
        while (*s != '\0' && *s != '=' && !is_space((unsigned char)*s) && *s != '?') {
            s++;
        }
        size_t kn = (size_t)(s - key);
        while (is_space((unsigned char)*s)) {
            s++;
        }
        if (*s != '=') {
            return -1;
        }
        s++;
        while (is_space((unsigned char)*s)) {
            s++;
        }
        if (*s != '"' && *s != '\'') {
            return -1;
        }
        char quote = *s++;
        const char *val = s;
        while (*s != '\0' && *s != quote) {
            s++;
        }
        if (*s != quote) {
            return -1;
        }
        size_t vn = (size_t)(s - val);
        s++;
        if (eq_token(key, kn, "version")) {
            if (!eq_token(val, vn, "1.0")) {
                return -1;
            }
            saw_version = 1;
        } else if (eq_token(key, kn, "encoding")) {
            int utf8 = eq_token(val, vn, "utf-8") || eq_token(val, vn, "UTF-8");
            if (!utf8) {
                return -1;
            }
        } else if (eq_token(key, kn, "standalone")) {
            if (!eq_token(val, vn, "yes") && !eq_token(val, vn, "no")) {
                return -1;
            }
        } else {
            return -1;
        }
    }
    if (*s != '?' || !saw_version) {
        return -1;
    }
    return 0;
}

/* Return XML_EV_NONE when markup was skipped (comment, declaration, PI). */
static int parse_markup(XmlParser *p, XmlEvent *ev) {
    p->bracket_run = 0;
    if (nextch(p) != '<') {
        return fail(p, ev, "internal markup");
    }
    int n = peek(p);
    if (n < 0) {
        return fail(p, ev, "truncated markup");
    }
    if (n == '/') {
        p->document_start = 0;
        return parse_end(p, ev);
    }
    if (n == '?') {
        int at_start = p->document_start;
        p->document_start = 0;
        nextch(p);
        char body[256];
        int bn = 0;
        int prev = 0;
        for (;;) {
            int c = nextch(p);
            if (c < 0) {
                return fail_at(p, ev, "truncated processing instruction");
            }
            if (p->utf8_bad) {
                return fail_at(p, ev, "illegal character");
            }
            if (prev == '?' && c == '>') {
                break;
            }
            if (bn >= (int)sizeof body - 1) {
                return fail_at(p, ev, "processing instruction too long");
            }
            body[bn++] = (char)c;
            prev = c;
        }
        body[bn] = '\0';
        int xmlish = bn >= 3 && (body[0] == 'x' || body[0] == 'X') && (body[1] == 'm' || body[1] == 'M') &&
                     (body[2] == 'l' || body[2] == 'L');
        int exact = bn >= 4 && strncmp(body, "xml", 3) == 0 && (is_space((unsigned char)body[3]) || body[3] == '?');
        if (xmlish && !exact) {
            return fail_at(p, ev, "reserved processing instruction");
        }
        if (exact) {
            if (!at_start) {
                return fail_at(p, ev, "xml declaration not at start");
            }
            if (xml_decl_ok(body) != 0) {
                return fail_at(p, ev, "bad xml declaration");
            }
        }
        return XML_EV_NONE;
    }
    if (n == '!') {
        p->document_start = 0;
        nextch(p);
        int is_cdata = match_at(p, "[CDATA[", 7);
        if (is_cdata < 0) {
            return fail(p, ev, "read error");
        }
        if (is_cdata == 1) {
            if (p->depth == 0) {
                return fail_at(p, ev, "cdata outside element");
            }
            if (consume_n(p, 7) < 0) {
                return fail(p, ev, "truncated cdata");
            }
            p->in_cdata = 1;
            p->cdata_mark = 0;
            return parse_cdata(p, ev);
        }
        return skip_bang(p, ev);
    }
    p->document_start = 0;
    return parse_start(p, ev);
}

static int consume_bom(XmlParser *p) {
    p->bom_checked = 1;
    while (p->len - p->pos < 3 && !p->eof) {
        size_t before = p->len - p->pos;
        if (fill(p) < 0) {
            return -1;
        }
        if (p->len - p->pos <= before) {
            break;
        }
    }
    if (p->len - p->pos >= 3) {
        unsigned char *b = (unsigned char *)p->buf + p->pos;
        if (b[0] == 0xFF && b[1] == 0xFE) {
            return -2;
        }
        if (b[0] == 0xFE && b[1] == 0xFF) {
            return -2;
        }
        if (b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
            p->pos += 3;
            p->offset += 3;
        }
    }
    return 0;
}

int xml_parser_init(XmlParser *p, XmlReadFn read, void *user) {
    memset(p, 0, sizeof *p);
    p->read = read;
    p->user = user;
    p->document_start = 1;
    return 0;
}

static int file_read(void *user, char *dst, int max) {
    FILE *fp = user;
    size_t n = fread(dst, 1, (size_t)max, fp);
    if (n == 0 && ferror(fp)) {
        return -1;
    }
    return (int)n;
}

int xml_parser_init_file(XmlParser *p, void *file) {
    return xml_parser_init(p, file_read, file);
}

int xml_next(XmlParser *p, XmlEvent *ev) {
    ev->kind = XML_EV_NONE;
    ev->nattr = 0;
    ev->text_len = 0;
    ev->from_cdata = 0;
    ev->name[0] = '\0';
    ev->text[0] = '\0';
    ev->error[0] = '\0';

    if (p->failed) {
        ev->kind = XML_EV_ERROR;
        ev->byte_offset = p->offset;
        snprintf(ev->error, sizeof ev->error, "%s", p->errmsg);
        return XML_EV_ERROR;
    }
    if (p->pending_end) {
        p->pending_end = 0;
        snprintf(ev->name, sizeof ev->name, "%s", p->end_name);
        int pr = pop_name(p, p->end_name);
        if (pr != 0) {
            return fail(p, ev, "empty element mismatch");
        }
        ev->kind = XML_EV_END;
        ev->byte_offset = p->offset;
        return XML_EV_END;
    }
    if (p->done) {
        ev->kind = XML_EV_EOF;
        ev->byte_offset = p->offset;
        return XML_EV_EOF;
    }
    if (!p->bom_checked) {
        int bom = consume_bom(p);
        if (bom == -2) {
            return fail_at(p, ev, "unsupported encoding");
        }
        if (bom < 0) {
            return fail(p, ev, "read error");
        }
    }

    for (;;) {
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (p->in_cdata) {
            int ck = parse_cdata(p, ev);
            if (ck == XML_EV_NONE) {
                continue;
            }
            return ck;
        }
        int c = peek(p);
        if (c == -1) {
            return fail(p, ev, "read error");
        }
        if (c == -2) {
            if (p->utf8_need != 0) {
                return fail_at(p, ev, "illegal character");
            }
            if (p->depth != 0) {
                return fail(p, ev, "unclosed element");
            }
            if (!p->seen_root) {
                return fail_at(p, ev, "no document element");
            }
            p->done = 1;
            ev->kind = XML_EV_EOF;
            ev->byte_offset = p->offset;
            return XML_EV_EOF;
        }
        if (c == '<') {
            int k = parse_markup(p, ev);
            if (k == XML_EV_NONE) {
                continue;
            }
            return k;
        }
        return parse_text(p, ev);
    }
}
