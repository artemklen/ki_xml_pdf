/* Supported XML subset regressions: Char/UTF-8 in every consumed context,
   references, PI, declaration, DOCTYPE and skipped DTD, read-size and
   chunk-boundary invariance, late tails and public XML causes.
   Included from test_stream.c, which provides CHECK, ChunkIn/chunk_read,
   MemIo/mem_read/mem_write and the temporary-file helpers. */

#define XS_LIT(s) (s), (sizeof(s) - 1)
#define XS_PROJ_CAP (64 * 1024)

typedef struct {
    const char *b;
    size_t n;
} XsBytes;

typedef struct {
    int ok;
    char *proj;
    size_t len;
    int split_scalar;
    char err[160];
} XsRun;

static const size_t xs_caps[] = {1, 2, 4095, 4096};

static void xs_put(XsRun *r, const char *s, size_t n) {
    if (r->len + n > XS_PROJ_CAP) {
        n = XS_PROJ_CAP - r->len;
    }
    memcpy(r->proj + r->len, s, n);
    r->len += n;
}

static int xs_ends_mid_scalar(const char *s, size_t n) {
    size_t i = n;
    int cont = 0;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80 && cont < 4) {
        i--;
        cont++;
    }
    if (i == 0) {
        return cont > 0;
    }
    unsigned char lead = (unsigned char)s[i - 1];
    int need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    return need != cont;
}

/* Projects events as "<name a=v>", text bytes and "</name>". */
static void xs_run(const char *s, size_t n, size_t cap, XsRun *r) {
    ChunkIn in;
    memset(&in, 0, sizeof in);
    in.s = s;
    in.n = n;
    in.limit = cap;
    r->ok = 0;
    r->len = 0;
    r->split_scalar = 0;
    r->err[0] = '\0';
    XmlParser p;
    xml_parser_init(&p, chunk_read, &in);
    for (;;) {
        XmlEvent ev;
        int k = xml_next(&p, &ev);
        if (k == XML_EV_ERROR) {
            snprintf(r->err, sizeof r->err, "%s", ev.error);
            return;
        }
        if (k == XML_EV_EOF) {
            r->ok = in.i == in.n;
            return;
        }
        if (k == XML_EV_START) {
            xs_put(r, "<", 1);
            xs_put(r, ev.name, strlen(ev.name));
            for (int a = 0; a < ev.nattr; a++) {
                xs_put(r, " ", 1);
                xs_put(r, ev.attr_name[a], strlen(ev.attr_name[a]));
                xs_put(r, "=", 1);
                xs_put(r, ev.attr_val[a], strlen(ev.attr_val[a]));
            }
            xs_put(r, ">", 1);
        } else if (k == XML_EV_END) {
            xs_put(r, "</", 2);
            xs_put(r, ev.name, strlen(ev.name));
            xs_put(r, ">", 1);
        } else if (k == XML_EV_TEXT) {
            if (ev.text_len > 0 && (((unsigned char)ev.text[0] & 0xC0) == 0x80 ||
                                    xs_ends_mid_scalar(ev.text, ev.text_len))) {
                r->split_scalar = 1;
            }
            xs_put(r, ev.text, ev.text_len);
        }
    }
}

static void xs_show(const char *s, size_t n) {
    size_t lim = n > 120 ? 120 : n;
    fputc('[', stderr);
    for (size_t i = 0; i < lim; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F) {
            fputc(c, stderr);
        } else {
            fprintf(stderr, "\\x%02X", c);
        }
    }
    fprintf(stderr, "%s] (%zu bytes)\n", n > lim ? "..." : "", n);
}

/* Same verdict, projection and whole-scalar text events at every read cap.
   Returns 0 when the input was accepted with the expected projection (or
   any projection when proj is NULL), 1 when rejected at every cap, and -1
   when the caps disagree. */
static int xs_verdict(const char *s, size_t n, const char *proj, size_t proj_n) {
    XsRun first;
    XsRun r;
    memset(&first, 0, sizeof first);
    memset(&r, 0, sizeof r);
    first.proj = malloc(XS_PROJ_CAP);
    r.proj = malloc(XS_PROJ_CAP);
    int verdict = -1;
    if (first.proj == NULL || r.proj == NULL) {
        goto out;
    }
    for (size_t c = 0; c < sizeof xs_caps / sizeof xs_caps[0]; c++) {
        XsRun *cur = c == 0 ? &first : &r;
        xs_run(s, n, xs_caps[c], cur);
        if (cur->ok && cur->split_scalar) {
            fprintf(stderr, "cap %zu: text event split a UTF-8 scalar\n", xs_caps[c]);
            goto out;
        }
        if (c > 0 && (r.ok != first.ok || (r.ok && (r.len != first.len || memcmp(r.proj, first.proj, r.len) != 0)))) {
            fprintf(stderr, "cap %zu disagrees with cap 1 (%s / %s)\n", xs_caps[c], first.err, r.err);
            goto out;
        }
    }
    if (!first.ok) {
        verdict = 1;
        goto out;
    }
    if (proj != NULL && (first.len != proj_n || memcmp(first.proj, proj, proj_n) != 0)) {
        fprintf(stderr, "projection mismatch, got ");
        xs_show(first.proj, first.len);
        goto out;
    }
    verdict = 0;
out:
    if (verdict == 1 && first.err[0] == '\0') {
        verdict = -1;
    }
    free(first.proj);
    free(r.proj);
    return verdict;
}

static int xs_expect(const char *s, size_t n, int want, const char *proj, size_t proj_n) {
    int got = xs_verdict(s, n, proj, proj_n);
    if (got != want) {
        fprintf(stderr, "expected %s, got %d for ", want == 0 ? "accept" : "reject", got);
        xs_show(s, n);
        return 1;
    }
    return 0;
}

#define XS_OK(s) CHECK(xs_expect(XS_LIT(s), 0, NULL, 0) == 0)
#define XS_OKP(s, proj) CHECK(xs_expect(XS_LIT(s), 0, XS_LIT(proj)) == 0)
#define XS_BAD(s) CHECK(xs_expect(XS_LIT(s), 1, NULL, 0) == 0)

/* Replaces the first '@' of tpl with the given bytes. */
static size_t xs_fill(char *out, size_t cap, const char *tpl, const char *b, size_t bn) {
    const char *at = strchr(tpl, '@');
    size_t pre = (size_t)(at - tpl);
    size_t post = strlen(at + 1);
    if (pre + bn + post > cap) {
        return 0;
    }
    memcpy(out, tpl, pre);
    memcpy(out + pre, b, bn);
    memcpy(out + pre + bn, at + 1, post);
    return pre + bn + post;
}

static const XsBytes xs_illegal[] = {
    {"\x00", 1}, {"\x01", 1}, {"\x08", 1}, {"\x0B", 1}, {"\x0C", 1}, {"\x1F", 1},
    {"\xEF\xBF\xBE", 3}, {"\xEF\xBF\xBF", 3},
    /* Malformed UTF-8: stray continuations, overlongs, surrogates, out of
       range, invalid leads and truncated sequences. */
    {"\x80", 1}, {"\xBF", 1}, {"\xC0\x80", 2}, {"\xC1\xBF", 2}, {"\xE0\x80\x80", 3},
    {"\xE0\x9F\xBF", 3}, {"\xF0\x80\x80\x80", 4}, {"\xF0\x8F\xBF\xBF", 4},
    {"\xED\xA0\x80", 3}, {"\xED\xBF\xBF", 3}, {"\xF4\x90\x80\x80", 4},
    {"\xF5\x80\x80\x80", 4}, {"\xF8\x88\x80\x80\x80", 5}, {"\xFE", 1}, {"\xFF", 1},
    {"\xC3", 1}, {"\xE2\x82", 2}, {"\xF0\x9F\x98", 3}, {"\xC3\xC3\xA9", 3}, {"\xE2\x82\xC3\xA9", 4},
};

static const XsBytes xs_legal[] = {
    {"\x09", 1}, {"\x0A", 1}, {" ", 1}, {"\x7F", 1}, {"\xC2\x80", 2}, {"\xD0\x96", 2},
    {"\xED\x9F\xBF", 3}, {"\xEE\x80\x80", 3}, {"\xEF\xBF\xBD", 3}, {"\xF0\x90\x80\x80", 4},
    {"\xF4\x8F\xBF\xBF", 4},
};

static const char *const xs_contexts[] = {
    "<a>@</a>",
    "<a>x@y</a>",
    "<a><b>@</b></a>",
    "<a v=\"@\"/>",
    "<a v='x@'/>",
    "<a><![CDATA[@]]></a>",
    "<a><![CDATA[x@]]]></a>",
    "<a><!--@--></a>",
    "<!--@--><a/>",
    "<a/><!--@-->",
    "<a><?pi @?></a>",
    "<?pi @?><a/>",
    "<a/><?pi @?>",
    "<?xml version=\"1.0\"?><!--@--><a/>",
    "<!DOCTYPE a [<!--@-->]><a/>",
    "<!DOCTYPE a [<!ENTITY e \"@\">]><a/>",
    "<!DOCTYPE a [<!ATTLIST a v CDATA '@'>]><a/>",
    "<!DOCTYPE a [<?pi @?>]><a/>",
    "<!DOCTYPE a SYSTEM \"@\"><a/>",
    "<!DOCTYPE a [<!ELEMENT a ANY>]><a>@</a>",
};

static int test_xml_char_contexts(void) {
    char buf[256];
    for (size_t c = 0; c < sizeof xs_contexts / sizeof xs_contexts[0]; c++) {
        for (size_t i = 0; i < sizeof xs_illegal / sizeof xs_illegal[0]; i++) {
            size_t n = xs_fill(buf, sizeof buf, xs_contexts[c], xs_illegal[i].b, xs_illegal[i].n);
            CHECK(n > 0);
            CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
        }
        for (size_t i = 0; i < sizeof xs_legal / sizeof xs_legal[0]; i++) {
            size_t n = xs_fill(buf, sizeof buf, xs_contexts[c], xs_legal[i].b, xs_legal[i].n);
            CHECK(n > 0);
            CHECK(xs_expect(buf, n, 0, NULL, 0) == 0);
        }
    }
    /* Element and attribute names consume the same validated bytes. */
    for (size_t i = 0; i < sizeof xs_illegal / sizeof xs_illegal[0]; i++) {
        size_t n = xs_fill(buf, sizeof buf, "<a@/>", xs_illegal[i].b, xs_illegal[i].n);
        CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
        n = xs_fill(buf, sizeof buf, "<a b@=\"1\"/>", xs_illegal[i].b, xs_illegal[i].n);
        CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
        n = xs_fill(buf, sizeof buf, "<a@></a@>", xs_illegal[i].b, xs_illegal[i].n);
        CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
    }
    /* Legal boundaries decode to exactly their own bytes. */
    for (size_t i = 0; i < sizeof xs_legal / sizeof xs_legal[0]; i++) {
        char proj[32];
        size_t n = xs_fill(buf, sizeof buf, "<a>@</a>", xs_legal[i].b, xs_legal[i].n);
        size_t pn = xs_fill(proj, sizeof proj, "<a>@</a>", xs_legal[i].b, xs_legal[i].n);
        CHECK(xs_expect(buf, n, 0, proj, pn) == 0);
        n = xs_fill(buf, sizeof buf, "<a><![CDATA[@]]></a>", xs_legal[i].b, xs_legal[i].n);
        CHECK(xs_expect(buf, n, 0, proj, pn) == 0);
    }
    XS_OKP("<a>x\ry\r\nz</a>", "<a>x\ny\nz</a>");
    XS_OKP("<a v=\"x\ty\nz\"/>", "<a v=x y z></a>");
    /* EOF inside a sequence after the root. */
    XS_BAD("<a/>\xC3");
    XS_BAD("<a/>\n\xE2\x82");
    return 0;
}

static int test_xml_references(void) {
    XS_OKP("<a>&amp;&lt;&gt;&quot;&apos;</a>", "<a>&<>\"'</a>");
    XS_OKP("<a v=\"&amp;&lt;&gt;&quot;&apos;\"/>", "<a v=&<>\"'></a>");
    XS_OKP("<a>&#65;&#x41;&#x6a;&#x6A;&#0065;&#x000041;&#9;&#10;&#13;&#32;</a>", "<a>AAjjAA\t\n\r </a>");
    XS_OKP("<a>&#xD7FF;&#xE000;&#xFFFD;&#x10000;&#x10FFFF;&#1114111;</a>",
           "<a>\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBD\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\xF4\x8F\xBF\xBF</a>");
    XS_OKP("<a v=\"&#1046;&#x416;\"/>", "<a v=\xD0\x96\xD0\x96></a>");
    XS_OKP("<a><![CDATA[&amp;&#65;&bogus;&#0;]]></a>", "<a>&amp;&#65;&bogus;&#0;</a>");
    XS_OKP("<a>&#38;amp;&#38;#0;</a>", "<a>&amp;&#0;</a>");

    static const char *const bad[] = {
        "&foo;", "&nbsp;", "&amp", "&amp ;", "&;", "& amp;", "&AMP;", "&Amp;", "&am;", "&ampx;",
        "&#X41;", "&#0;", "&#x0;", "&#00;", "&#1;", "&#x1F;", "&#xB;", "&#12;", "&#xD800;",
        "&#xDFFF;", "&#55296;", "&#xFFFE;", "&#xFFFF;", "&#65535;", "&#x110000;", "&#1114112;",
        "&#99999999999999999999;", "&#xFFFFFFFFFFFFFFFF41;", "&#;", "&#x;", "&#12a;", "&#xG;",
        "&#-1;", "&#+65;", "&# 65;", "&#65 ;", "&#x 41;", "&#65", "&#x41", "&", "&#", "&#x",
    };
    char buf[128];
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        size_t n = xs_fill(buf, sizeof buf, "<a>@</a>", bad[i], strlen(bad[i]));
        CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
        n = xs_fill(buf, sizeof buf, "<a v=\"@\"/>", bad[i], strlen(bad[i]));
        CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
        n = xs_fill(buf, sizeof buf, "<a>@", bad[i], strlen(bad[i]));
        CHECK(xs_expect(buf, n, 1, NULL, 0) == 0);
    }
    XS_BAD("<a>&lt");
    XS_BAD("<a/>&amp;");
    XS_BAD("<a/>&#32;x");
    return 0;
}

/* Exact OSR-11 audit inputs, kept byte-for-byte in tests/corpus/xml. */
static const struct {
    const char *file;
    const char *xml;
    size_t n;
    int bad;
} xs_audited[] = {
    {"osr11-text-fffe.xml", XS_LIT("<root>\xEF\xBF\xBE</root>"), 1},
    {"osr11-comment-ffff.xml", XS_LIT("<root><!--\xEF\xBF\xBF--></root>"), 1},
    {"osr11-doctype-twice.xml", XS_LIT("<!DOCTYPE root[]> <!DOCTYPE root[]> <root/>"), 1},
    {"osr11-dtd-comment-bracket.xml", XS_LIT("<!DOCTYPE root [ <!-- ] --> ]><root/>"), 0},
    {"osr11-xml-stylesheet.xml", XS_LIT("<?xml-stylesheet type=\"text/xsl\" href=\"view.xsl\"?><root/>"), 0},
    {"osr11-pi-empty-target.xml", XS_LIT("<?\?><root/>"), 1},
    {"osr11-pi-digit-target.xml", XS_LIT("<?1bad?><root/>"), 1},
    {"osr11-decl-version-twice.xml", XS_LIT("<?xml version=\"1.0\" version=\"1.0\"?><root/>"), 1},
    {"osr11-decl-encoding-first.xml", XS_LIT("<?xml encoding=\"UTF-8\" version=\"1.0\"?><root/>"), 1},
    {"osr11-decl-no-space.xml", XS_LIT("<?xml version=\"1.0\"encoding=\"UTF-8\"?><root/>"), 1},
    {"osr11-doctype-no-space.xml", XS_LIT("<!DOCTYPEfoo><root/>"), 1},
};

static int test_xml_audited_corpus(void) {
    for (size_t i = 0; i < sizeof xs_audited / sizeof xs_audited[0]; i++) {
        char path[256];
        snprintf(path, sizeof path, "tests/corpus/xml/%s", xs_audited[i].file);
        char *data = NULL;
        size_t len = 0;
        CHECK(load_file(path, &data, &len) == 0);
        int same = len == xs_audited[i].n && memcmp(data, xs_audited[i].xml, len) == 0;
        free(data);
        if (!same) {
            fprintf(stderr, "corpus file differs from audited bytes: %s\n", path);
            return 1;
        }
        int want = xs_audited[i].bad;
        const char *proj = want == 0 ? "<root></root>" : NULL;
        CHECK(xs_expect(xs_audited[i].xml, xs_audited[i].n, want, proj, proj ? strlen(proj) : 0) == 0);
    }
    return 0;
}

static int test_xml_pi_and_declaration(void) {
    XS_OKP("<?xml-stylesheet type=\"text/xsl\" href=\"view.xsl\"?><root/>", "<root></root>");
    XS_OKP("<?xml-model href=\"m.rnc\"?><root/>", "<root></root>");
    XS_OKP("<?xml version=\"1.0\"?><?xml-stylesheet href=\"a.css\"?><root/>", "<root></root>");
    XS_OKP("<?pi?><root/>", "<root></root>");
    XS_OKP("<?pi ?><root/>", "<root></root>");
    XS_OKP("<root>a<?pi x?y>z ? > ?\?>b</root>", "<root>ab</root>");
    XS_OKP("<?XmL-x data?><?xmlfoo?><?Xml.y?><root/>", "<root></root>");
    XS_OKP("<root/><?tail data?>\n<!-- end -->", "<root></root>\n");
    XS_OKP("<?pi \xD0\x96 \xF4\x8F\xBF\xBF?><root/>", "<root></root>");

    XS_BAD("<?\?><root/>");
    XS_BAD("<?1bad?><root/>");
    XS_BAD("<? pi?><root/>");
    XS_BAD("<?pi|x?><root/>");
    XS_BAD("<?pi?x?><root/>");
    XS_BAD("<?XML version=\"1.0\"?><root/>");
    XS_BAD("<?Xml version=\"1.0\"?><root/>");
    XS_BAD("<?xmL version=\"1.0\"?><root/>");
    XS_BAD("<?XML?><root/>");
    XS_BAD("<root><?XML x?></root>");
    XS_BAD("<root/><?xml version=\"1.0\"?>");
    XS_BAD("<root><?xml version=\"1.0\"?></root>");
    XS_BAD("<!--c--><?xml version=\"1.0\"?><root/>");
    XS_BAD("<?pi?><?xml version=\"1.0\"?><root/>");
    XS_BAD(" <?xml version=\"1.0\"?><root/>");
    XS_BAD("<?xml version=\"1.0\"?><?xml version=\"1.0\"?><root/>");
    XS_BAD("<?pi data");
    XS_BAD("<?pi data?");
    XS_BAD("<?pi");
    XS_BAD("<?");
    XS_BAD("<root><?pi data</root>");
    XS_BAD("<root/><?pi");

    XS_OKP("<?xml version=\"1.0\"?><root/>", "<root></root>");
    XS_OK("<?xml version='1.0'?><root/>");
    XS_OK("<?xml version=\"1.0\" ?><root/>");
    XS_OK("<?xml version = \"1.0\"  encoding = 'utf-8'  standalone = \"no\" ?><root/>");
    XS_OK("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>");
    XS_OK("<?xml version=\"1.0\" standalone=\"yes\"?><root/>");
    XS_OK("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><root/>");
    XS_OK("<?xml\tversion=\"1.0\"\r\nencoding=\"UTF-8\"\n?>\n<root/>");
    XS_OK("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>");
    XS_OK("\xEF\xBB\xBF<root/>");

    XS_BAD("<?xml version=\"1.0\" version=\"1.0\"?><root/>");
    XS_BAD("<?xml encoding=\"UTF-8\" version=\"1.0\"?><root/>");
    XS_BAD("<?xml version=\"1.0\"encoding=\"UTF-8\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" standalone=\"yes\" encoding=\"UTF-8\"?><root/>");
    XS_BAD("<?xml standalone=\"yes\" version=\"1.0\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" encoding=\"UTF-8\" encoding=\"UTF-8\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" standalone=\"yes\" standalone=\"yes\"?><root/>");
    XS_BAD("<?xml version=\"1.0\"standalone=\"yes\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" encoding=\"UTF-8\"standalone=\"no\"?><root/>");
    XS_BAD("<?xml encoding=\"UTF-8\"?><root/>");
    XS_BAD("<?xml?><root/>");
    XS_BAD("<?xml ?><root/>");
    XS_BAD("<?xml version=\"1.1\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" encoding=\"UTF-16\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" encoding=\"latin1\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" standalone=\"maybe\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" foo=\"bar\"?><root/>");
    XS_BAD("<?xml version='1.0\"?><root/>");
    XS_BAD("<?xml version=1.0?><root/>");
    XS_BAD("<?xml version \"1.0\"?><root/>");
    XS_BAD("<?xml version=\"1.0\" ?x><root/>");
    XS_BAD("<?xml version=\"1.0\"");
    XS_BAD("<?xml version=\"1.0\"?");
    XS_BAD("<?xml version=\"1.0");
    XS_BAD("\xEF\xBB\xBF <?xml version=\"1.0\"?><root/>");
    XS_BAD("\xEF\xBB\xBF\xEF\xBB\xBF<root/>");
    return 0;
}

static int test_xml_doctype_and_dtd(void) {
    XS_OKP("<!DOCTYPE root><root/>", "<root></root>");
    XS_OK("<!DOCTYPE root[]><root/>");
    XS_OKP("<!DOCTYPE root [ <!-- ] --> ]><root/>", "<root></root>");
    XS_OK("<!DOCTYPE root SYSTEM \"x.dtd\"><root/>");
    XS_OK("<!DOCTYPE root SYSTEM 'a\"b>]'><root/>");
    XS_OKP("<!DOCTYPE root PUBLIC \"-//X//DTD X 1.0//EN\" \"http://127.0.0.1:9/x.dtd\"><root>ok</root>",
           "<root>ok</root>");
    XS_OK("<!DOCTYPE root PUBLIC \"-//X//'q'//EN\" 's'><root/>");
    XS_BAD("<!DOCTYPE root PUBLIC '-//X//\"q\"//EN' 's'><root/>");
    XS_OK("<!DOCTYPE root SYSTEM \"x.dtd\" [ ]  ><root/>");
    XS_OKP("<!DOCTYPE root [ <!ENTITY e \"a]>b\"> <!ATTLIST root a CDATA '>]'> <!ELEMENT root ANY>\n"
           "  <!NOTATION n SYSTEM \"n\"> <?pi ]> ?> %pe; <!-- c --> <!ENTITY % q '<!--'> ]><root/>",
           "<root></root>");
    XS_OK("<?xml version=\"1.0\"?>\n<!-- c -->\n<?pi?>\n<!DOCTYPE root>\n<!-- d -->\n<root/>");
    XS_OK("\xEF\xBB\xBF<!DOCTYPE root><root/>");

    XS_BAD("<!DOCTYPE root[]> <!DOCTYPE root[]> <root/>");
    XS_BAD("<!DOCTYPE root><!DOCTYPE root><root/>");
    XS_BAD("<!DOCTYPEfoo><root/>");
    XS_BAD("<!DOCTYPE><root/>");
    XS_BAD("<!DOCTYPE >< root/>");
    XS_BAD("<!doctype root><root/>");
    XS_BAD("<!DOCTYPE root");
    XS_BAD("<!DOCTYPE root [");
    XS_BAD("<!DOCTYPE root []");
    XS_BAD("<!DOCTYPE root [] <root/>");
    XS_BAD("<!DOCTYPE root [ ] ]><root/>");
    XS_BAD("<root/><!DOCTYPE root>");
    XS_BAD("<root><!DOCTYPE root></root>");
    XS_BAD("<!DOCTYPE root SYSTEM><root/>");
    XS_BAD("<!DOCTYPE root SYSTEM \"a\"x><root/>");
    XS_BAD("<!DOCTYPE root SYSTEM\"a\"><root/>");
    XS_BAD("<!DOCTYPE root SYSTEM \"unterminated><root/>");
    XS_BAD("<!DOCTYPE root PUBLIC \"p\"><root/>");
    XS_BAD("<!DOCTYPE root PUBLIC \"p\"\"s\"><root/>");
    XS_BAD("<!DOCTYPE root PUBLIC \"a{b\" \"s\"><root/>");
    XS_BAD("<!DOCTYPE root FOO \"a\"><root/>");
    XS_BAD("<!DOCTYPE root SYSTEMX \"a\"><root/>");
    XS_BAD("<!DOCTYPE root [ <!-- ] -- > ]><root/>");
    XS_BAD("<!DOCTYPE root [ <!-- unterminated ]><root/>");
    XS_BAD("<!DOCTYPE root [ <?pi ]><root/>");
    XS_BAD("<!DOCTYPE root [ <!ENTITY e \"]>\" ><root/>");
    XS_BAD("<!DOCTYPE root [ <!ENTITY e \"unterminated ]><root/>");
    XS_BAD("<!DOCTYPE root [ %pe ]><root/>");
    XS_BAD("<!DOCTYPE root [ junk ]><root/>");
    XS_BAD("<!DOCTYPE root [<!FOO x>]><root/>");
    XS_BAD("<!DOCTYPE root [<!ELEMENT root <b>>]><root/>");
    XS_BAD("<!DOCTYPE root [<!ELEMENTroot ANY>]><root/>");
    XS_BAD("<!DOCTYPE root [<?xml version=\"1.0\"?>]><root/>");
    XS_BAD("<!DOCTYPE root [<![INCLUDE[ ]]>]><root/>");
    XS_BAD("<!DOCTYPE root [<!ELEMENT root ANY>");

    /* Declared and external entities are never fetched or expanded, so a
       reference to them stays an unknown entity. */
    XS_BAD("<!DOCTYPE root [<!ENTITY e \"SENTINEL\">]><root>&e;</root>");
    XS_BAD("<!DOCTYPE root [<!ENTITY e \"SENTINEL\">]><root a=\"&e;\"/>");
    XS_BAD("<!DOCTYPE root [<!ENTITY ext SYSTEM \"http://127.0.0.1:9/ext\">]><root>&ext;</root>");
    XS_BAD("<!DOCTYPE root SYSTEM \"file:///etc/passwd\"><root>&passwd;</root>");
    XS_OKP("<!DOCTYPE root SYSTEM \"file:///etc/passwd\" [<!ENTITY amp2 \"SENTINEL\">]><root>x</root>",
           "<root>x</root>");
    return 0;
}

/* A large root-free tail stays validated after the document element. */
static int test_xml_late_tails(void) {
    static const char *const bad_tail[] = {
        "<!--\x01-->", "<?pi \xEF\xBF\xBF?>", "&amp;", "&#1;", "<b/>", "<!-- -- -->", "<?1?>", "<?pi",
        "\xC3", "<!--", "x", "<![CDATA[x]]>", "<!DOCTYPE b>", "</a>", "\xEF\xBF\xBE",
    };
    size_t pad = 5000;
    size_t cap = 3 + pad + 64;
    char *buf = malloc(cap);
    CHECK(buf != NULL);
    memcpy(buf, "<a/>", 4);
    memset(buf + 4, ' ', pad);
    for (size_t i = 0; i < sizeof bad_tail / sizeof bad_tail[0]; i++) {
        size_t tn = strlen(bad_tail[i]);
        memcpy(buf + 4 + pad, bad_tail[i], tn);
        if (xs_expect(buf, 4 + pad + tn, 1, NULL, 0) != 0) {
            free(buf);
            return 1;
        }
    }
    const char good[] = "<!-- ok --><?pi ok?>\n";
    memcpy(buf + 4 + pad, good, sizeof good - 1);
    int rc = xs_expect(buf, 4 + pad + sizeof good - 1, 0, NULL, 0);
    free(buf);
    CHECK(rc == 0);
    return 0;
}

typedef struct {
    const char *src;
    const char *proj;
} XsPiece;

/* Pieces slide across the 1023-byte text chunk and the 4096-byte window. */
static int test_xml_chunk_boundaries(void) {
    static const XsPiece good[] = {
        {"\xD0\x96", "\xD0\x96"},
        {"\xE2\x82\xAC", "\xE2\x82\xAC"},
        {"\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80"},
        {"\xF4\x8F\xBF\xBF\xD0\x96", "\xF4\x8F\xBF\xBF\xD0\x96"},
        {"&amp;&lt;", "&<"},
        {"&#x10FFFF;", "\xF4\x8F\xBF\xBF"},
        {"&#1046;", "\xD0\x96"},
        {"<![CDATA[\xD0\x96]]]>", "\xD0\x96]"},
        {"<!--\xD0\x96-->", ""},
        {"<?pi \xD0\x96?>", ""},
        {"<b x=\"\xD0\x96&lt;\"></b>", "<b x=\xD0\x96<></b>"},
        {"\r\n", "\n"},
    };
    static const char *const bad[] = {
        "\xEF\xBF\xBE", "\xED\xA0\x80", "\xC3x", "&#xFFFE;", "&bogus;", "&amp", "<!--\x01-->",
        "<?pi \x0B?>", "<![CDATA[\xEF\xBF\xBF]]>", "<b x=\"\xEF\xBF\xBF\"/>", "]]>", "<!-- -- -->",
    };
    size_t starts[2] = {XML_TEXT_MAX - 1 - 8, XML_BUF_SIZE - 3 - 8};
    size_t cap = XML_BUF_SIZE + 128;
    char *doc = malloc(cap);
    char *proj = malloc(cap);
    CHECK(doc != NULL && proj != NULL);
    int rc = 0;
    for (int s = 0; s < 2 && rc == 0; s++) {
        for (size_t k = starts[s]; k <= starts[s] + 10 && rc == 0; k++) {
            memcpy(doc, "<a>", 3);
            memset(doc + 3, 'A', k);
            memcpy(proj, doc, 3 + k);
            for (size_t i = 0; i < sizeof good / sizeof good[0] && rc == 0; i++) {
                size_t sn = strlen(good[i].src);
                size_t pn = strlen(good[i].proj);
                memcpy(doc + 3 + k, good[i].src, sn);
                memcpy(doc + 3 + k + sn, "B</a>", 5);
                memcpy(proj + 3 + k, good[i].proj, pn);
                memcpy(proj + 3 + k + pn, "B</a>", 5);
                rc = xs_expect(doc, 3 + k + sn + 5, 0, proj, 3 + k + pn + 5);
            }
            for (size_t i = 0; i < sizeof bad / sizeof bad[0] && rc == 0; i++) {
                size_t sn = strlen(bad[i]);
                memcpy(doc + 3 + k, bad[i], sn);
                memcpy(doc + 3 + k + sn, "B</a>", 5);
                rc = xs_expect(doc, 3 + k + sn + 5, 1, NULL, 0);
            }
        }
    }
    free(doc);
    free(proj);
    CHECK(rc == 0);
    return 0;
}

/* Callback conversion at read sizes 1 and 4096 has the expected cause. */
static int xs_cause(const char *xml, size_t n, const KiOptions *base, int cause) {
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
        ki_convert(&opt, &result);
        free(io.buf);
        if (result.cause != cause) {
            fprintf(stderr, "chunk %d: cause %d (%s), wanted %d for ", chunks[c], result.cause, result.message,
                    cause);
            xs_show(xml, n);
            return 1;
        }
    }
    return 0;
}

static int xs_entries(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) {
        return -1;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

static int xs_pdftotext(const char *pdf, char *text, size_t cap) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "pdftotext -layout -q %s -", pdf);
    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        return -1;
    }
    size_t n = fread(text, 1, cap - 1, pipe);
    text[n] = '\0';
    return pclose(pipe) == 0 ? 0 : -1;
}

static int test_xml_public_causes(void) {
    KiOptions opt;
    ki_options_init(&opt);
    /* Illegal or malformed scalars are XML, not FONT, in text and in
       content no mapper renders. */
    CHECK(xs_cause(XS_LIT("<root>\xEF\xBF\xBE</root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root>\xEF\xBF\xBF</root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root><!--\xEF\xBF\xBF--></root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root a=\"\xEF\xBF\xBE\"/>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root>\xED\xA0\x80</root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root>\xE2\x82</root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root>&#xFFFE;</root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root>&#X41;</root>"), &opt, KI_CAUSE_XML) == 0);
    /* A legal scalar the font lacks remains FONT. */
    CHECK(xs_cause(XS_LIT("<root>&#x4E2D;</root>"), &opt, KI_CAUSE_FONT) == 0);
    CHECK(xs_cause(XS_LIT("<root>\xE4\xB8\xAD</root>"), &opt, KI_CAUSE_FONT) == 0);
    for (size_t i = 0; i < sizeof xs_audited / sizeof xs_audited[0]; i++) {
        int want = xs_audited[i].bad ? KI_CAUSE_XML : KI_CAUSE_NONE;
        CHECK(xs_cause(xs_audited[i].xml, xs_audited[i].n, &opt, want) == 0);
    }
    opt.map = text_only_map;
    CHECK(xs_cause(XS_LIT("<root>ok<!--\xEF\xBF\xBF--></root>"), &opt, KI_CAUSE_XML) == 0);
    CHECK(xs_cause(XS_LIT("<root>ok</root><?pi \x01?>"), &opt, KI_CAUSE_XML) == 0);

    /* PI data is not rendered. */
    char dir[] = "/tmp/ki-xml-subset-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char xml_path[256];
    char pdf_path[256];
    snprintf(xml_path, sizeof xml_path, "%s/in.xml", dir);
    snprintf(pdf_path, sizeof pdf_path, "%s/out.pdf", dir);
    const char pi_doc[] = "<?xml-stylesheet type=\"text/xsl\" href=\"view.xsl\"?>"
                          "<root>visible<?pi SECRET?>tail</root>";
    CHECK(write_all(xml_path, pi_doc, sizeof pi_doc - 1) == 0);
    KiResult result;
    CHECK(ki_convert_file(xml_path, pdf_path, NULL, &result) == 0);
    char text[1024];
    CHECK(xs_pdftotext(pdf_path, text, sizeof text) == 0);
    CHECK(strstr(text, "visibletail") != NULL);
    CHECK(strstr(text, "view.xsl") == NULL && strstr(text, "SECRET") == NULL && strstr(text, "xsl") == NULL);
    unlink(xml_path);
    unlink(pdf_path);
    rmdir(dir);
    return 0;
}

/* Errors in records skipped by report selection and after the root fail as
   XML at every read size and never publish a file destination. */
static int test_xml_selected_tails(void) {
    static const char *const poison[] = {
        "\xEF\xBF\xBE", "&#1;", "&bogus;", "<!-- -- -->", "<?1bad?>", "<?pi \xEF\xBF\xBF?>", "\xC3x",
    };
    char dir[] = "/tmp/ki-xml-tail-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char dest[256];
    snprintf(dest, sizeof dest, "%s/out.pdf", dir);
    size_t cap = 64 * 1024;
    char *xml = malloc(cap);
    CHECK(xml != NULL);
    int rc = 0;
    for (int where = 0; where < 2 && rc == 0; where++) {
        for (size_t i = 0; i < sizeof poison / sizeof poison[0] && rc == 0; i++) {
            size_t n = 0;
            n += (size_t)snprintf(xml + n, cap - n, "<report version=\"1\" title=\"t\">");
            for (int r = 1; r <= 120; r++) {
                n += (size_t)snprintf(xml + n, cap - n, "<record id=\"%d\"><field name=\"n\">%s%s</field></record>",
                                      r, "value", where == 0 && r == 100 ? poison[i] : "");
            }
            n += (size_t)snprintf(xml + n, cap - n, "</report>%s", where == 1 ? "\n" : "");
            if (where == 1) {
                n += (size_t)snprintf(xml + n, cap - n, "%s", poison[i]);
            }
            if (n <= XML_BUF_SIZE) {
                rc = 1;
                break;
            }
            CHECK(write_all(dest, "OLD", 3) == 0);
            int chunks[3] = {1, 4095, 4096};
            for (int c = 0; c < 3 && rc == 0; c++) {
                MemIo io;
                memset(&io, 0, sizeof io);
                io.s = xml;
                io.n = n;
                io.chunk = chunks[c];
                KiOptions opt;
                ki_options_init(&opt);
                opt.profile = KI_PROFILE_REPORT;
                opt.record_limit = 1;
                opt.temp_dir = dir;
                opt.read_bytes = mem_read;
                opt.io_user = &io;
                KiResult result;
                int st = ki_convert_to_file(dest, &opt, &result);
                if (st != XML2PDF_ERR_XML || result.cause != KI_CAUSE_XML || !same_bytes(dest, "OLD", 3) ||
                    xs_entries(dir) != 1) {
                    fprintf(stderr, "late %s poison %zu chunk %d: status %d cause %d (%s)\n",
                            where == 0 ? "record" : "tail", i, chunks[c], st, result.cause, result.message);
                    rc = 1;
                }
            }
        }
    }
    /* The same selection without poison publishes. */
    if (rc == 0) {
        size_t n = 0;
        n += (size_t)snprintf(xml + n, cap - n, "<report version=\"1\" title=\"t\">");
        for (int r = 1; r <= 120; r++) {
            n += (size_t)snprintf(xml + n, cap - n, "<record id=\"%d\"><field name=\"n\">value</field></record>", r);
        }
        n += (size_t)snprintf(xml + n, cap - n, "</report>\n<!-- end --><?pi end?>");
        CHECK(write_all(dest, "OLD", 3) == 0);
        MemIo io;
        memset(&io, 0, sizeof io);
        io.s = xml;
        io.n = n;
        io.chunk = 1;
        KiOptions opt;
        ki_options_init(&opt);
        opt.profile = KI_PROFILE_REPORT;
        opt.record_limit = 1;
        opt.temp_dir = dir;
        opt.read_bytes = mem_read;
        opt.io_user = &io;
        KiResult result;
        if (ki_convert_to_file(dest, &opt, &result) != 0 || result.report_total != 120 ||
            result.report_selected != 1 || same_bytes(dest, "OLD", 3)) {
            fprintf(stderr, "clean selection failed: %s\n", result.message);
            rc = 1;
        }
    }
    free(xml);
    unlink(dest);
    rmdir(dir);
    CHECK(rc == 0);
    return 0;
}
