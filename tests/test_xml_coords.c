/* Diagnostic coordinates: 1-based line and Unicode-scalar column and 0-based
   raw byte offset of the consumed input, for parser events and errors,
   built-in profile rules and custom ki_doc_fail, across read caps.
   Included from test_stream.c after test_xml_subset.c (CHECK, ChunkIn,
   MemIo, XS_LIT, xs_caps, xs_show). */

#include <stdint.h>

/* Independent reference for the documented convention, applied to a
   consumed prefix: a UTF-8 BOM at offset 0 adds no column; CR, LF and CRLF
   each end one line; every other byte starts a column unless it is a
   continuation byte of an open multibyte sequence. */
static void xc_oracle(const char *s, size_t n, size_t *line, size_t *col) {
    size_t i = 0;
    size_t l = 1;
    size_t c = 1;
    int need = 0;
    int cr = 0;
    if (n >= 3 && memcmp(s, "\xEF\xBB\xBF", 3) == 0) {
        i = 3;
    }
    for (; i < n; i++) {
        unsigned char b = (unsigned char)s[i];
        if (need > 0 && (b & 0xC0) == 0x80) {
            need--;
            cr = 0;
            continue;
        }
        need = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
        if (b == '\r' || (b == '\n' && !cr)) {
            l++;
            c = 1;
        } else if (b != '\n') {
            c++;
        }
        cr = b == '\r';
    }
    *line = l;
    *col = c;
}

typedef struct {
    const char *label;
    const char *in;
    size_t in_n;
    size_t consumed;
    size_t line;
    size_t col;
    const char *reason; /* NULL = parses to EOF */
    int paddable;
} XcCase;

#define XC(label, lit, consumed, line, col, reason, pad) \
    {label, lit, sizeof(lit) - 1, consumed, line, col, reason, pad}

static const XcCase xc_cases[] = {
    XC("eof after crlf", "<r/>\r\n", 6, 2, 1, NULL, 1),
    XC("bom refs cdata tab eof", "\xEF\xBB\xBF<r>\tx&amp;<![CDATA[\xD0\x96&lt;]]>\r</r>\n", 37, 3, 1, NULL, 0),
    XC("unclosed after crlf", "<r>\r\n<a>", 8, 2, 4, "unclosed element", 1),
    XC("lone cr and crlf", "<r>\rx\r\r\n&lt;", 12, 4, 5, "unclosed element", 1),
    XC("bad char reference", "<r>\n\t&#0;</r>", 9, 2, 6, "bad character reference", 1),
    XC("bad reference digit", "<r>&#1x;</r>", 6, 1, 7, "bad character reference", 1),
    XC("unknown entity before amp", "<r>&foo;</r>", 3, 1, 4, "unknown entity", 1),
    XC("raw lt in attribute", "<a b='x<'/>", 7, 1, 8, "raw '<' in attribute", 1),
    XC("utf8 bad continuation", "<r>\xC3\xA9\xC3\x28</r>", 7, 1, 7, "illegal character", 1),
    XC("utf8 overlong", "<r>\xE0\x80\x80</r>", 5, 1, 5, "illegal character", 1),
    XC("utf8 surrogate", "<r>\xED\xA0\x80</r>", 5, 1, 5, "illegal character", 1),
    XC("utf8 non-char U+FFFE", "<r>\xEF\xBF\xBE</r>", 6, 1, 5, "illegal character", 1),
    XC("utf8 stray continuation", "<r>a\x80</r>", 5, 1, 6, "illegal character", 1),
    XC("utf8 lead F5", "<r>\xF5</r>", 4, 1, 5, "illegal character", 1),
    XC("utf8 truncated at eof", "<r>\xE2\x82", 5, 1, 5, "illegal character", 1),
    /* Detection is context-dependent: tags and names are checked only after
       the enclosing construct, so these positions lie past the bad byte. */
    XC("utf8 truncated before lt", "<r>\xC3</r>", 4, 1, 5, "illegal character", 1),
    /* A text reference is consumed whole: its '&' ends the open sequence,
       but the mark is checked only after ';', or the reference error wins. */
    XC("utf8 truncated before named reference", "<r>\xC3&amp;</r>", 9, 1, 10, "illegal character", 1),
    XC("utf8 truncated before decimal reference", "<r>\xC3&#38;</r>", 9, 1, 10, "illegal character", 1),
    XC("utf8 truncated before hex reference", "<r>\xC3&#x26;</r>", 10, 1, 11, "illegal character", 1),
    XC("utf8 truncated before two references", "<r>\xE2\x82&lt;&gt;</r>", 9, 1, 9, "illegal character", 1),
    XC("utf8 stray continuation before reference", "<r>\x80&amp;</r>", 4, 1, 5, "illegal character", 1),
    XC("utf8 truncated before unknown entity", "<r>\xC3&bogus;</r>", 4, 1, 5, "unknown entity", 1),
    XC("utf8 truncated before bad reference", "<r>\xC3&#0;</r>", 8, 1, 9, "bad character reference", 1),
    XC("utf8 truncated before reference in attribute", "<r a='\xC3&amp;'/>", 15, 1, 16, "illegal character", 1),
    XC("utf8 attribute value empty tag", "<r a='\x80" "abc'/>", 13, 1, 14, "illegal character", 1),
    XC("utf8 attribute value start tag", "<r a='\x80" "abc'>x</r>", 12, 1, 13, "illegal character", 1),
    XC("utf8 attribute value then missing eq", "<r a='\x80" "abc' b/>", 14, 1, 15, "attribute without value", 1),
    XC("utf8 element name", "<r\x80 a='1'/>", 11, 1, 12, "illegal character", 1),
    XC("utf8 attribute name", "<r a\x80='1'/>", 11, 1, 12, "illegal character", 1),
    XC("utf8 end tag name", "<r></r\x80>", 8, 1, 9, "illegal character", 1),
    XC("utf8 cdata", "<r><![CDATA[a\x80" "b]]></r>", 14, 1, 15, "illegal character", 1),
    XC("utf8 comment", "<!--a\x80" "b--><r/>", 6, 1, 7, "illegal character", 1),
    XC("utf8 pi data", "<?pi a\x80" "b?><r/>", 7, 1, 8, "illegal character", 1),
    XC("utf8 pi target", "<?p\x80i a?><r/>", 5, 1, 6, "illegal character", 1),
    XC("utf8 xml declaration value", "<?xml version='1\x80.0'?><r/>", 20, 1, 21, "illegal character", 0),
    XC("utf8 doctype name", "<!DOCTYPE r\x80><r/>", 13, 1, 14, "illegal character", 1),
    XC("utf8 doctype name before subset", "<!DOCTYPE r\x80 [<!ELEMENT r ANY>]><r/>", 15, 1, 16,
       "illegal character", 1),
    XC("utf8 doctype system literal", "<!DOCTYPE r SYSTEM 'a\x80" "b'><r/>", 22, 1, 23, "illegal character", 1),
    XC("utf8 markup declaration", "<!DOCTYPE r [<!ELEMENT r (a\x80" "b)>]><r/>", 28, 1, 29, "illegal character", 1),
    XC("utf8 after root", "<r/>\n\x80", 6, 2, 2, "illegal character", 1),
    XC("bom then comment control", "\xEF\xBB\xBF<!-- \x01 -->", 9, 1, 7, "illegal character", 0),
    XC("cdata keeps spelling", "<r><![CDATA[&amp;\r\n\xD0\x96]]>&#1;</r>", 28, 2, 9, "bad character reference", 1),
    XC("doctype then second root",
       "<?xml version=\"1.0\"?>\n<!DOCTYPE r [\n<!-- c -->\n<!ENTITY x 'y'>\n]>\n<r/>\n<r/>", 75, 7, 5,
       "multiple document elements", 0),
    XC("end tag mismatch", "<a>\n</b >", 9, 2, 6, "end tag mismatch", 1),
    XC("utf16 bom", "\xFF\xFE<r/>", 0, 1, 1, "unsupported encoding", 0),
    XC("no document element", "\xEF\xBB\xBF\n", 4, 2, 1, "no document element", 0),
};

static void xc_message(char *out, size_t cap, const char *reason, size_t line, size_t col, size_t byte) {
    snprintf(out, cap, "%s at line %zu, column %zu, byte %zu", reason, line, col, byte);
}

/* Every event and the failure must carry the coordinates of exactly the
   consumed prefix [0, byte_offset). */
static int xc_run(const char *s, size_t n, size_t cap, const XcCase *want, size_t shift) {
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = s;
    in.n = n;
    in.limit = cap;
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    size_t prev = 0;
    for (int guard = 0; guard < 100000; guard++) {
        XmlEvent ev;
        memset(&ev, 0x5a, sizeof ev);
        int k = xml_next(&p, &ev);
        size_t ol, oc;
        if (ev.byte_offset > n || ev.byte_offset < prev) {
            fprintf(stderr, "%s cap %zu: offset %zu (prev %zu, n %zu)\n", want->label, cap, ev.byte_offset, prev, n);
            return 1;
        }
        prev = ev.byte_offset;
        xc_oracle(s, ev.byte_offset, &ol, &oc);
        if (ev.line != ol || ev.column != oc) {
            fprintf(stderr, "%s cap %zu kind %d: %zu:%zu at byte %zu, oracle %zu:%zu\n", want->label, cap, k, ev.line,
                    ev.column, ev.byte_offset, ol, oc);
            return 1;
        }
        if (k == XML_EV_ERROR || k == XML_EV_EOF) {
            size_t wl = want->line, wc = want->col;
            if (shift > 0) {
                xc_oracle(s, shift + want->consumed, &wl, &wc);
            }
            int ok = ev.byte_offset == shift + want->consumed && ev.line == wl && ev.column == wc;
            if (want->reason == NULL) {
                ok = ok && k == XML_EV_EOF && ev.byte_offset == n;
            } else {
                char msg[160];
                xc_message(msg, sizeof msg, want->reason, wl, wc, shift + want->consumed);
                ok = ok && k == XML_EV_ERROR && strcmp(ev.error, msg) == 0;
                /* A repeated call reports the same failure. */
                XmlEvent again;
                xml_next(&p, &again);
                ok = ok && again.kind == XML_EV_ERROR && strcmp(again.error, ev.error) == 0 &&
                     again.line == ev.line && again.column == ev.column && again.byte_offset == ev.byte_offset;
            }
            if (!ok) {
                fprintf(stderr, "%s cap %zu shift %zu: kind %d \"%s\" %zu:%zu byte %zu, want %zu:%zu byte %zu\n",
                        want->label, cap, shift, k, k == XML_EV_ERROR ? ev.error : "", ev.line, ev.column,
                        ev.byte_offset, wl, wc, shift + want->consumed);
                return 1;
            }
            return 0;
        }
    }
    return 1;
}

static int test_xml_coordinates(void) {
    size_t ncase = sizeof xc_cases / sizeof xc_cases[0];
    for (size_t i = 0; i < ncase; i++) {
        const XcCase *c = &xc_cases[i];
        /* The hardcoded triple and the independent oracle must agree. */
        size_t ol, oc;
        CHECK(c->consumed <= c->in_n);
        xc_oracle(c->in, c->consumed, &ol, &oc);
        if (ol != c->line || oc != c->col) {
            fprintf(stderr, "fixture %s: oracle %zu:%zu, table %zu:%zu\n", c->label, ol, oc, c->line, c->col);
            return 1;
        }
        for (size_t k = 0; k < sizeof xs_caps / sizeof xs_caps[0]; k++) {
            CHECK(xc_run(c->in, c->in_n, xs_caps[k], c, 0) == 0);
        }
        if (!c->paddable) {
            continue;
        }
        /* A leading comment of two-byte scalars moves the interesting bytes
           across the 4096-byte window and read boundaries. */
        static char buf[12000];
        for (size_t target = 4090; target <= 4100; target++) {
            size_t body = target - 7;
            size_t len = 0;
            memcpy(buf, "<!--", 4);
            len = 4;
            while (len < 4 + body) {
                if (4 + body - len >= 2 && (len % 3) != 0) {
                    memcpy(buf + len, "\xD0\x96", 2);
                    len += 2;
                } else {
                    buf[len++] = 'a';
                }
            }
            memcpy(buf + len, "-->", 3);
            len += 3;
            size_t shift = len;
            memcpy(buf + len, c->in, c->in_n);
            len += c->in_n;
            for (size_t k = 0; k < sizeof xs_caps / sizeof xs_caps[0]; k++) {
                CHECK(xc_run(buf, len, xs_caps[k], c, shift) == 0);
            }
        }
    }
    return 0;
}

/* Position arithmetic refuses to wrap: a counter at its maximum fails the
   parse with a fixed reason instead of continuing with a wrapped value. */
static int xc_limit(const char *xml, size_t start_offset, size_t start_line, size_t start_col) {
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = xml;
    in.n = strlen(xml);
    in.limit = 1;
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    p.offset = start_offset;
    p.line = start_line;
    p.column = start_col;
    size_t prev = start_offset;
    for (int guard = 0; guard < 64; guard++) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        CHECK(ev.byte_offset >= prev && ev.line >= start_line);
        CHECK(ev.line != start_line || ev.column >= start_col);
        prev = ev.byte_offset;
        if (k == XML_EV_ERROR) {
            CHECK(p.pos_full);
            CHECK(strncmp(ev.error, "input position limit at line ", 29) == 0);
            return 0;
        }
        CHECK(k != XML_EV_EOF);
    }
    return 1;
}

static int test_xml_coordinate_limits(void) {
    CHECK(xc_limit("<r>abc</r>", SIZE_MAX - 4, 1, 1) == 0);
    CHECK(xc_limit("\xEF\xBB\xBF<r/>", SIZE_MAX - 1, 1, 1) == 0);
    CHECK(xc_limit("<r>\n</r>", 0, SIZE_MAX, 1) == 0);
    CHECK(xc_limit("<r>\r</r>", 0, SIZE_MAX, 1) == 0);
    CHECK(xc_limit("<r>&amp;</r>", 0, 1, SIZE_MAX - 4) == 0);
    /* Near but not at the limit, the counters stay exact. */
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = "<r>\xD0\x96</r>";
    in.n = strlen(in.s);
    in.limit = 1;
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    p.offset = SIZE_MAX - in.n;
    p.column = SIZE_MAX - 9;
    XmlEvent ev;
    int k;
    do {
        k = xml_next(&p, &ev);
    } while (k != XML_EV_EOF && k != XML_EV_ERROR);
    CHECK(k == XML_EV_EOF && ev.byte_offset == SIZE_MAX && ev.column == SIZE_MAX - 1 && !p.pos_full);
    return 0;
}

/* Public API: full message and cause at read sizes 1 and 4096. */
static int xc_public(const char *xml, size_t n, const KiOptions *base, int cause, const char *want) {
    int chunks[2] = {1, 4096};
    for (int c = 0; c < 2; c++) {
        MemIo io;
        memset(&io, 0, sizeof io);
        io.s = xml;
        io.n = n;
        io.chunk = chunks[c];
        KiOptions opt = *base;
        opt.read_bytes = mem_read;
        opt.write_bytes = mem_write;
        opt.io_user = &io;
        KiResult result;
        memset(&result, 0x5a, sizeof result);
        ki_convert(&opt, &result);
        free(io.buf);
        size_t len = strnlen(result.message, sizeof result.message);
        if (result.cause != cause || len >= sizeof result.message || strcmp(result.message, want) != 0) {
            fprintf(stderr, "chunk %d: cause %d \"%.*s\", wanted %d \"%s\"\n", chunks[c], result.cause,
                    (int)sizeof result.message, result.message, cause, want);
            return 1;
        }
    }
    return 0;
}

/* Expected message for a failure right after the given consumed prefix. */
static void xc_expect(char *out, size_t cap, const char *rule, const char *xml, const char *until) {
    const char *at = strstr(xml, until);
    size_t consumed = at != NULL ? (size_t)(at - xml) + strlen(until) : 0;
    size_t line, col;
    xc_oracle(xml, consumed, &line, &col);
    xc_message(out, cap, rule, line, col, consumed);
}

static const struct {
    const char *xml;
    const char *until;
    const char *rule;
    long offset;
    long limit;
} xc_profile_cases[] = {
    {"<report version=\"2\" title=\"T\"><record><field name=\"n\">a</field></record></report>",
     "title=\"T\">", "profile: report version", 0, 0},
    {"<report version=\"1\" title=\"Отчёт\">\n  <record>\n    <field name=\"имя\">знач</field>\n  </record>\n"
     "  <bogus/>\n</report>",
     "<bogus/>", "profile: unknown element", 0, 0},
    {"<report version=\"1\" title=\"T\">\r\n<record id=\"1\" extra=\"z\"><field name=\"n\">a</field></record></report>",
     "extra=\"z\">", "profile: unknown attribute extra on record", 0, 0},
    {"<report version=\"1\" title=\"T\">\n\t<record/>\n</report>", "<record/>", "profile: empty record", 0, 0},
    {"<report version=\"1\" title=\"T\">\n<record><field name=\"n\">a</field></record>\nтекст</report>",
     "\nтекст", "profile: structural text", 0, 0},
    {"<report version=\"1\" title=\"T\"><record><field name=\"n\">a</field></record>\n"
     "<record><field name=\"n\">b</field></record>\n<record><field name=\"n\" zz=\"1\">c</field></record></report>",
     "zz=\"1\">", "profile: unknown attribute zz on field", 1, 1},
    {"<report version=\"1\" title=\"T\"><record><field name=\"n\">a</field></record>\n"
     "<record><field name=\"n\"><field name=\"m\"/></field></record></report>",
     "<field name=\"m\"/>", "profile: field nesting", 0, 1},
};

static int test_profile_coordinates(void) {
    char want[160];
    for (size_t i = 0; i < sizeof xc_profile_cases / sizeof xc_profile_cases[0]; i++) {
        KiOptions opt;
        ki_options_init(&opt);
        opt.profile = KI_PROFILE_REPORT;
        opt.record_offset = xc_profile_cases[i].offset;
        opt.record_limit = xc_profile_cases[i].limit;
        const char *xml = xc_profile_cases[i].xml;
        CHECK(strstr(xml, xc_profile_cases[i].until) != NULL);
        xc_expect(want, sizeof want, xc_profile_cases[i].rule, xml, xc_profile_cases[i].until);
        CHECK(xc_public(xml, strlen(xml), &opt, KI_CAUSE_PROFILE, want) == 0);
    }
    /* Hardcoded triples for the multiline Cyrillic and CRLF fixtures. */
    xc_expect(want, sizeof want, "r", xc_profile_cases[1].xml, xc_profile_cases[1].until);
    CHECK(strcmp(want, "r at line 5, column 11, byte 115") == 0);
    xc_expect(want, sizeof want, "r", xc_profile_cases[2].xml, xc_profile_cases[2].until);
    CHECK(strcmp(want, "r at line 2, column 26, byte 57") == 0);

    /* XML causes keep their own reason and gain the same triple. */
    KiOptions opt;
    ki_options_init(&opt);
    const char *bad = "<r>\n  <a>\xC3\x28</a></r>";
    CHECK(xc_public(bad, strlen(bad), &opt, KI_CAUSE_XML, "xml: illegal character at line 2, column 8, byte 11") == 0);
    const char *named = "<r>\xC3&amp;</r>";
    CHECK(xc_public(named, strlen(named), &opt, KI_CAUSE_XML, "xml: illegal character at line 1, column 10, byte 9") == 0);
    const char *numeric = "<r>\xC3&#38;</r>";
    CHECK(xc_public(numeric, strlen(numeric), &opt, KI_CAUSE_XML, "xml: illegal character at line 1, column 10, byte 9") ==
          0);
    const char *eof = "<r>\r\n<a>";
    CHECK(xc_public(eof, strlen(eof), &opt, KI_CAUSE_XML, "xml: unclosed element at line 2, column 4, byte 8") == 0);
    /* Font failures are not relabelled and carry no XML coordinates. */
    const char *glyph = "<r>\n&#x1F600;</r>";
    CHECK(xc_public(glyph, strlen(glyph), &opt, KI_CAUSE_FONT, "font: U+1F600") == 0);
    return 0;
}

typedef struct {
    int kind;
    int nth;
    int seen;
    const char *rule;
} XcFailMap;

static int xc_fail_map(void *user, KiDoc *doc, const KiEvent *ev) {
    XcFailMap *m = user;
    if (ev->kind == KI_EV_START && strcmp(ev->name, "doc") == 0) {
        m->seen = 0;
    }
    if (ev->kind == m->kind && ++m->seen == m->nth) {
        return ki_doc_fail(doc, m->rule);
    }
    if (ev->kind == KI_EV_TEXT) {
        return ki_doc_text(doc, ev->text, ev->text_len);
    }
    return ev->kind == KI_EV_END ? ki_doc_break(doc) : 0;
}

static int xc_custom(const char *xml, int kind, int nth, const char *rule, const char *until, const char *shown) {
    XcFailMap m = {kind, nth, 0, rule};
    KiOptions opt;
    ki_options_init(&opt);
    opt.map = xc_fail_map;
    opt.map_user = &m;
    char want[160];
    xc_expect(want, sizeof want, shown, xml, until);
    return xc_public(xml, strlen(xml), &opt, KI_CAUSE_PROFILE, want);
}

static int test_custom_fail_coordinates(void) {
    const char *xml = "<doc>\n  <item a=\"1\">Жук &amp; ёж</item>\r\n  <empty/>\n  <tail>x</tail>\n</doc>";
    CHECK(xc_custom(xml, KI_EV_START, 2, "catalog: item rule", "<item a=\"1\">", "catalog: item rule") == 0);
    CHECK(xc_custom(xml, KI_EV_TEXT, 2, "catalog: text rule", "ёж", "catalog: text rule") == 0);
    CHECK(xc_custom(xml, KI_EV_END, 1, "catalog: end rule", "</item>", "catalog: end rule") == 0);
    /* The synthetic END of an empty element uses the START position. */
    CHECK(xc_custom(xml, KI_EV_END, 2, "catalog: synthetic end", "<empty/>", "catalog: synthetic end") == 0);
    CHECK(xc_custom(xml, KI_EV_START, 3, NULL, "<empty/>", "mapping failed") == 0);

    /* Long rules are cut so the whole triple fits; the cut never splits a
       UTF-8 scalar and the start of the rule survives. */
    char ascii[400];
    memset(ascii, 'R', sizeof ascii - 1);
    ascii[sizeof ascii - 1] = '\0';
    memcpy(ascii, "rule-id-42 ", 11);
    char cyr[400];
    size_t cn = 0;
    memcpy(cyr, "правило-7 ", strlen("правило-7 "));
    cn = strlen("правило-7 ");
    while (cn + 2 < sizeof cyr) {
        memcpy(cyr + cn, "Ж", 2);
        cn += 2;
    }
    cyr[cn] = '\0';
    const char *rules[2] = {ascii, cyr};
    const char *ids[2] = {"rule-id-42 ", "правило-7 "};
    for (int r = 0; r < 2; r++) {
        XcFailMap m = {KI_EV_START, 2, 0, rules[r]};
        KiOptions opt;
        ki_options_init(&opt);
        opt.map = xc_fail_map;
        opt.map_user = &m;
        MemIo io;
        memset(&io, 0, sizeof io);
        io.s = xml;
        io.n = strlen(xml);
        io.chunk = 1;
        opt.read_bytes = mem_read;
        opt.write_bytes = mem_write;
        opt.io_user = &io;
        KiResult result;
        memset(&result, 0x5a, sizeof result);
        ki_convert(&opt, &result);
        free(io.buf);
        CHECK(result.cause == KI_CAUSE_PROFILE);
        size_t len = strnlen(result.message, sizeof result.message);
        CHECK(len < sizeof result.message);
        CHECK(strncmp(result.message, ids[r], strlen(ids[r])) == 0);
        char suffix[96];
        const char *at = strstr(xml, "<item a=\"1\">");
        size_t consumed = (size_t)(at - xml) + strlen("<item a=\"1\">");
        size_t line, col;
        xc_oracle(xml, consumed, &line, &col);
        snprintf(suffix, sizeof suffix, " at line %zu, column %zu, byte %zu", line, col, consumed);
        size_t sl = strlen(suffix);
        CHECK(len > sl + strlen(ids[r]) && strcmp(result.message + len - sl, suffix) == 0);
        CHECK(len + 4 >= sizeof result.message - 1);
        /* The rule part is whole UTF-8. */
        const unsigned char *u = (const unsigned char *)result.message;
        size_t rl = len - sl;
        CHECK(!xs_ends_mid_scalar(result.message, rl));
        for (size_t i = 0; i < rl; i++) {
            CHECK(u[i] != 0);
        }
    }

    /* A reused result carrying a coordinate message is reset by an early
       usage error. */
    KiResult result;
    XcFailMap m = {KI_EV_START, 1, 0, "first"};
    KiOptions opt;
    ki_options_init(&opt);
    opt.map = xc_fail_map;
    opt.map_user = &m;
    MemIo io;
    memset(&io, 0, sizeof io);
    io.s = xml;
    io.n = strlen(xml);
    io.chunk = 7;
    opt.read_bytes = mem_read;
    opt.write_bytes = mem_write;
    opt.io_user = &io;
    ki_convert(&opt, &result);
    free(io.buf);
    CHECK(result.cause == KI_CAUSE_PROFILE && strcmp(result.message, "first at line 1, column 6, byte 5") == 0);
    opt.size = 1;
    CHECK(ki_convert(&opt, &result) == XML2PDF_ERR_USAGE);
    CHECK(result.cause == KI_CAUSE_USAGE && strcmp(result.message, "options size mismatch") == 0);
    return 0;
}
