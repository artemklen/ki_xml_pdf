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

static int xml_cp_ok(unsigned cp);

/* Every consumed byte passes through here, including bytes of skipped
   comments, PIs, DTD and records, so each completed scalar is checked
   against the XML Char production. */
static int feed_utf8(XmlParser *p, unsigned char c) {
    if (p->utf8_need == 0) {
        if (c < 0x80) {
            return xml_cp_ok(c) ? 0 : -1;
        }
        if ((c & 0xE0) == 0xC0) {
            if (c < 0xC2) {
                return -1;
            }
            p->utf8_need = 1;
            p->utf8_lead = c;
            p->utf8_cp = c & 0x1Fu;
            return 0;
        }
        if ((c & 0xF0) == 0xE0) {
            p->utf8_need = 2;
            p->utf8_lead = c;
            p->utf8_cp = c & 0x0Fu;
            return 0;
        }
        if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4) {
                return -1;
            }
            p->utf8_need = 3;
            p->utf8_lead = c;
            p->utf8_cp = c & 0x07u;
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
    p->utf8_cp = (p->utf8_cp << 6) | (c & 0x3Fu);
    p->utf8_need--;
    if (p->utf8_need == 0) {
        p->utf8_lead = 0;
        if (!xml_cp_ok(p->utf8_cp)) {
            return -1;
        }
    }
    return 0;
}

static int nextch(XmlParser *p) {
    int c = peek(p);
    if (c >= 0) {
        p->pos++;
        p->offset++;
        if (feed_utf8(p, (unsigned char)c) != 0) {
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
    if (c == 'x') {
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

/* A UTF-8 lead byte starts a new chunk unless the whole sequence fits, so
   no text event ends inside a scalar. */
static int lead_needs_room(int c, size_t n) {
    return c >= 0xC0 && n > 0 && n + 4 >= XML_TEXT_MAX;
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
            if (p->utf8_bad) {
                return fail_at(p, ev, "illegal character");
            }
            if (p->cdata_mark < 2) {
                p->cdata_mark++;
                continue;
            }
            ev->text[n++] = ']';
            continue;
        }
        if (c == '>' && p->cdata_mark == 2) {
            if (nextch(p) < 0) {
                return fail(p, ev, "read error");
            }
            if (p->utf8_bad) {
                return fail_at(p, ev, "illegal character");
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
        if (lead_needs_room(c, n)) {
            break;
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

static int fail_eof(XmlParser *p, XmlEvent *ev, int c, const char *msg) {
    if (c == -1) {
        return fail(p, ev, "read error");
    }
    return fail_at(p, ev, msg);
}

/* "<!--" has been consumed. */
static int comment_body(XmlParser *p, XmlEvent *ev) {
    int rc = skip_comment(p);
    if (rc == 0) {
        return XML_EV_NONE;
    }
    if (p->utf8_bad) {
        return fail_at(p, ev, "illegal character");
    }
    if (rc == -2) {
        return fail_at(p, ev, "bad comment");
    }
    return fail_at(p, ev, "truncated comment");
}

static int ascii_ieq(const char *s, const char *lower) {
    for (; *s != '\0' && *lower != '\0'; s++, lower++) {
        int c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        if (c != *lower) {
            return 0;
        }
    }
    return *s == '\0' && *lower == '\0';
}

/* Raw quoted pseudo-attribute value of the XML declaration. */
static int decl_value(XmlParser *p, char *out, int cap) {
    int q = nextch(p);
    if (q < 0) {
        return q;
    }
    if (q != '"' && q != '\'') {
        return -3;
    }
    int n = 0;
    for (;;) {
        int c = nextch(p);
        if (c < 0) {
            return c;
        }
        if (c == q) {
            out[n] = '\0';
            return n;
        }
        if (n + 1 >= cap) {
            return -3;
        }
        out[n++] = (char)c;
    }
}

/* "<?xml" has been consumed at the document start. Pseudo-attributes must
   be version, then optional encoding, then optional standalone, each once
   and each preceded by white space. */
static int parse_xml_decl(XmlParser *p, XmlEvent *ev) {
    static const char *const keys[3] = {"version", "encoding", "standalone"};
    int last = -1;
    for (;;) {
        int sw = skip_ws(p);
        if (sw < 0) {
            return fail_eof(p, ev, sw, "truncated xml declaration");
        }
        int c = peek(p);
        if (c == '?') {
            nextch(p);
            c = nextch(p);
            if (c < 0) {
                return fail_eof(p, ev, c, "truncated xml declaration");
            }
            if (c != '>' || last < 0) {
                return fail_at(p, ev, "bad xml declaration");
            }
            return XML_EV_NONE;
        }
        char key[16];
        if (sw == 0 || read_name(p, key, (int)sizeof key) <= 0) {
            return fail_at(p, ev, "bad xml declaration");
        }
        int k = -1;
        for (int i = 0; i < 3; i++) {
            if (strcmp(key, keys[i]) == 0) {
                k = i;
            }
        }
        if (k < 0 || k <= last || (last < 0 && k != 0)) {
            return fail_at(p, ev, "bad xml declaration");
        }
        last = k;
        sw = skip_ws(p);
        if (sw < 0) {
            return fail_eof(p, ev, sw, "truncated xml declaration");
        }
        if (peek(p) != '=') {
            return fail_at(p, ev, "bad xml declaration");
        }
        nextch(p);
        sw = skip_ws(p);
        if (sw < 0) {
            return fail_eof(p, ev, sw, "truncated xml declaration");
        }
        char val[16];
        int vn = decl_value(p, val, (int)sizeof val);
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (vn == -1 || vn == -2) {
            return fail_eof(p, ev, vn, "truncated xml declaration");
        }
        int ok = 0;
        if (vn >= 0 && k == 0) {
            ok = strcmp(val, "1.0") == 0;
        } else if (vn >= 0 && k == 1) {
            ok = strcmp(val, "UTF-8") == 0 || strcmp(val, "utf-8") == 0;
        } else if (vn >= 0) {
            ok = strcmp(val, "yes") == 0 || strcmp(val, "no") == 0;
        }
        if (!ok) {
            return fail_at(p, ev, "bad xml declaration");
        }
    }
}

/* "<?" has been consumed. The target must be a Name followed by white space
   or "?>". Only an exact lowercase "xml" target at the document start is a
   declaration; "xml" in any other case or place is reserved. PI data is
   never reported. */
static int parse_pi(XmlParser *p, XmlEvent *ev, int at_start) {
    char target[XML_NAME_MAX];
    int c = peek(p);
    if (c < 0) {
        return fail_eof(p, ev, c, "truncated processing instruction");
    }
    if (read_name(p, target, (int)sizeof target) <= 0) {
        return fail_at(p, ev, "bad processing instruction target");
    }
    if (p->utf8_bad) {
        return fail_at(p, ev, "illegal character");
    }
    if (ascii_ieq(target, "xml")) {
        if (strcmp(target, "xml") != 0) {
            return fail_at(p, ev, "reserved processing instruction");
        }
        if (!at_start) {
            return fail_at(p, ev, "xml declaration not at start");
        }
        return parse_xml_decl(p, ev);
    }
    c = peek(p);
    if (c < 0) {
        return fail_eof(p, ev, c, "truncated processing instruction");
    }
    if (c == '?') {
        nextch(p);
        c = nextch(p);
        if (c < 0) {
            return fail_eof(p, ev, c, "truncated processing instruction");
        }
        if (c != '>') {
            return fail_at(p, ev, "bad processing instruction target");
        }
        return XML_EV_NONE;
    }
    if (!is_space(c)) {
        return fail_at(p, ev, "bad processing instruction target");
    }
    int prev = 0;
    for (;;) {
        c = nextch(p);
        if (c < 0) {
            return fail_eof(p, ev, c, "truncated processing instruction");
        }
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (prev == '?' && c == '>') {
            return XML_EV_NONE;
        }
        prev = c;
    }
}

static int is_pubid_char(int c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    return c == ' ' || c == '\r' || c == '\n' || (c > 0 && c < 0x80 && strchr("-'()+,./:=?;!*#@$_%", c) != NULL);
}

/* Quoted SystemLiteral or PubidLiteral; the identifier is never resolved. */
static int skip_literal(XmlParser *p, XmlEvent *ev, int pubid) {
    int q = nextch(p);
    if (q < 0) {
        return fail_eof(p, ev, q, "truncated doctype");
    }
    if (q != '"' && q != '\'') {
        return fail_at(p, ev, "bad doctype");
    }
    for (;;) {
        int c = nextch(p);
        if (c < 0) {
            return fail_eof(p, ev, c, "truncated doctype");
        }
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (c == q) {
            return XML_EV_NONE;
        }
        if (pubid && !is_pubid_char(c)) {
            return fail_at(p, ev, "bad doctype");
        }
    }
}

/* Keyword of an ELEMENT/ATTLIST/ENTITY/NOTATION declaration has been
   consumed. Quoted values may hold '>' and ']'. Declared entities are
   discarded, so they never become valid references. */
static int skip_markup_decl(XmlParser *p, XmlEvent *ev) {
    int quote = 0;
    for (;;) {
        int c = nextch(p);
        if (c < 0) {
            return fail_eof(p, ev, c, "truncated doctype");
        }
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (quote) {
            if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '<') {
            return fail_at(p, ev, "bad doctype");
        } else if (c == '>') {
            return XML_EV_NONE;
        }
    }
}

/* '[' has been consumed; returns after the closing ']'. */
static int skip_internal_subset(XmlParser *p, XmlEvent *ev) {
    for (;;) {
        int sw = skip_ws(p);
        if (sw < 0) {
            return fail_eof(p, ev, sw, "truncated doctype");
        }
        int c = nextch(p);
        if (p->utf8_bad) {
            return fail_at(p, ev, "illegal character");
        }
        if (c == ']') {
            return XML_EV_NONE;
        }
        if (c == '%') {
            char name[XML_NAME_MAX];
            if (read_name(p, name, (int)sizeof name) <= 0 || nextch(p) != ';') {
                return fail_at(p, ev, "bad doctype");
            }
            continue;
        }
        if (c != '<') {
            return fail_at(p, ev, "bad doctype");
        }
        c = nextch(p);
        if (c < 0) {
            return fail_eof(p, ev, c, "truncated doctype");
        }
        int k;
        if (c == '?') {
            k = parse_pi(p, ev, 0);
        } else if (c != '!') {
            return fail_at(p, ev, "bad doctype");
        } else {
            int dash = match_at(p, "--", 2);
            if (dash < 0) {
                return fail(p, ev, "read error");
            }
            if (dash == 1) {
                consume_n(p, 2);
                k = comment_body(p, ev);
            } else {
                char kw[16];
                if (read_name(p, kw, (int)sizeof kw) <= 0 ||
                    (strcmp(kw, "ELEMENT") != 0 && strcmp(kw, "ATTLIST") != 0 && strcmp(kw, "ENTITY") != 0 &&
                     strcmp(kw, "NOTATION") != 0)) {
                    return fail_at(p, ev, "bad doctype");
                }
                c = peek(p);
                if (c < 0) {
                    return fail_eof(p, ev, c, "truncated doctype");
                }
                if (!is_space(c)) {
                    return fail_at(p, ev, "bad doctype");
                }
                k = skip_markup_decl(p, ev);
            }
        }
        if (k != XML_EV_NONE) {
            return k;
        }
    }
}

/* "<!DOCTYPE" has been consumed. One DOCTYPE is accepted before the root:
   S Name (S ExternalID)? S? ('[' intSubset ']' S?)? '>'. The external
   identifier is only checked for syntax; nothing is fetched. */
static int skip_doctype(XmlParser *p, XmlEvent *ev) {
    if (p->depth != 0 || p->seen_root) {
        return fail_at(p, ev, "doctype placement");
    }
    if (p->seen_doctype) {
        return fail_at(p, ev, "multiple doctype");
    }
    p->seen_doctype = 1;
    int sw = skip_ws(p);
    if (sw < 0) {
        return fail_eof(p, ev, sw, "truncated doctype");
    }
    char name[XML_NAME_MAX];
    if (sw == 0 || read_name(p, name, (int)sizeof name) <= 0) {
        return fail_at(p, ev, "bad doctype");
    }
    sw = skip_ws(p);
    if (sw < 0) {
        return fail_eof(p, ev, sw, "truncated doctype");
    }
    int c = peek(p);
    int k;
    if (c == 'S' || c == 'P') {
        char kw[8];
        if (sw == 0 || read_name(p, kw, (int)sizeof kw) <= 0) {
            return fail_at(p, ev, "bad doctype");
        }
        int pub = strcmp(kw, "PUBLIC") == 0;
        if (!pub && strcmp(kw, "SYSTEM") != 0) {
            return fail_at(p, ev, "bad doctype");
        }
        for (int lit = pub ? 2 : 1; lit > 0; lit--) {
            sw = skip_ws(p);
            if (sw < 0) {
                return fail_eof(p, ev, sw, "truncated doctype");
            }
            if (sw == 0) {
                return fail_at(p, ev, "bad doctype");
            }
            k = skip_literal(p, ev, pub && lit == 2);
            if (k != XML_EV_NONE) {
                return k;
            }
        }
        sw = skip_ws(p);
        if (sw < 0) {
            return fail_eof(p, ev, sw, "truncated doctype");
        }
        c = peek(p);
    }
    if (c == '[') {
        nextch(p);
        k = skip_internal_subset(p, ev);
        if (k != XML_EV_NONE) {
            return k;
        }
        sw = skip_ws(p);
        if (sw < 0) {
            return fail_eof(p, ev, sw, "truncated doctype");
        }
        c = peek(p);
    }
    if (c != '>') {
        return fail_at(p, ev, "bad doctype");
    }
    nextch(p);
    return XML_EV_NONE;
}

static int skip_bang(XmlParser *p, XmlEvent *ev) {
    int c1 = peek(p);
    if (c1 < 0) {
        return fail_eof(p, ev, c1, "truncated declaration");
    }
    if (c1 == '-') {
        if (nextch(p) < 0 || nextch(p) != '-') {
            return fail_at(p, ev, "bad comment");
        }
        return comment_body(p, ev);
    }
    int is_doc = match_at(p, "DOCTYPE", 7);
    if (is_doc < 0) {
        return fail(p, ev, "read error");
    }
    if (is_doc == 1) {
        consume_n(p, 7);
        return skip_doctype(p, ev);
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
            if (p->utf8_need != 0) {
                return fail_at(p, ev, "illegal character");
            }
            break;
        }
        if (lead_needs_room(c, n)) {
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
            if (p->utf8_bad) {
                return fail_at(p, ev, "illegal character");
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
    if (p->utf8_bad) {
        return fail_at(p, ev, "illegal character");
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
        return parse_pi(p, ev, at_start);
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
