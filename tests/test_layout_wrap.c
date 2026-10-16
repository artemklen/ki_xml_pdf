/* Word wrapping: break opportunities, oversized-word fallback, fragmentation
   and page transitions. Included from test_stream.c after
   test_font_metrics.c, which provides fm_build/fm_cmap/fm_write_font and
   fm_convert; test_stream.c provides CHECK, MemIo and put16/put32.

   The synthetic font fixes every advance, so expected lines follow from
   arithmetic, not from the converter: unitsPerEm 1000 makes the 532 pt line
   53200 units at 10 pt. */

#define LW_LIMIT 53200L
#define LW_GLYPHS 9

/* GID i + 1 maps lw_cps[i] (ascending, as cmap format 4 requires) with
   advance lw_adv[i]. */
static const unsigned lw_cps[LW_GLYPHS - 1] = {0x20, 0x41, 0x42, 0x69, 0xA0, 0x301, 0x416, 0x456};
static const unsigned lw_adv[LW_GLYPHS - 1] = {1330, 5320, 5321, 100, 1330, 0, 5320, 100};
static char lw_font_path[64];

static int lw_font(void) {
    if (lw_font_path[0] != '\0') {
        return 0;
    }
    static unsigned char head[54], hhea[36], maxp[6], hmtx[4 * LW_GLYPHS], cmap[256], ttf[FM_CAP];
    memset(head, 0, sizeof head);
    put16(head + 18, 1000);
    memset(hhea, 0, sizeof hhea);
    put16(hhea + 4, 800);
    put16(hhea + 6, 0xFF38);
    put16(hhea + 34, LW_GLYPHS);
    put32(maxp, 0x00005000ul);
    put16(maxp + 4, LW_GLYPHS);
    put16(hmtx, 500);
    put16(hmtx + 2, 0);
    unsigned gids[LW_GLYPHS - 1];
    for (int i = 0; i < LW_GLYPHS - 1; i++) {
        put16(hmtx + 4 * (i + 1), lw_adv[i]);
        put16(hmtx + 4 * (i + 1) + 2, 0);
        gids[i] = (unsigned)i + 1;
    }
    size_t cl = fm_cmap(cmap, lw_cps, gids, LW_GLYPHS - 1);
    FmTable t[5] = {{"head", head, sizeof head}, {"hhea", hhea, sizeof hhea}, {"maxp", maxp, sizeof maxp},
                    {"hmtx", hmtx, sizeof hmtx}, {"cmap", cmap, cl}};
    return fm_write_font(ttf, fm_build(ttf, t, 5), lw_font_path);
}

typedef struct {
    char *s;
    size_t n;
    size_t cap;
} LwBuf;

static int lw_put(LwBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap == 0 ? 4096 : b->cap;
        while (cap < b->n + n + 1) {
            cap *= 2;
        }
        char *g = realloc(b->s, cap);
        if (g == NULL) {
            return -1;
        }
        b->s = g;
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
    return 0;
}

static int lw_puts(LwBuf *b, const char *s) {
    return lw_put(b, s, strlen(s));
}

static int lw_rep(LwBuf *b, const char *unit, size_t k) {
    for (size_t i = 0; i < k; i++) {
        if (lw_puts(b, unit) != 0) {
            return -1;
        }
    }
    return 0;
}

static void lw_free(LwBuf *b) {
    free(b->s);
    memset(b, 0, sizeof *b);
}

static int lw_utf8(LwBuf *b, unsigned cp) {
    char u[4];
    size_t k;
    if (cp < 0x80) {
        u[0] = (char)cp;
        k = 1;
    } else if (cp < 0x800) {
        u[0] = (char)(0xC0 | (cp >> 6));
        u[1] = (char)(0x80 | (cp & 0x3F));
        k = 2;
    } else if (cp < 0x10000) {
        u[0] = (char)(0xE0 | (cp >> 12));
        u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[2] = (char)(0x80 | (cp & 0x3F));
        k = 3;
    } else {
        u[0] = (char)(0xF0 | (cp >> 18));
        u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        u[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[3] = (char)(0x80 | (cp & 0x3F));
        k = 4;
    }
    return lw_put(b, u, k);
}

/* The PDF's lines in order, read from each line's ActualText after
   independent stream decoding: "\n" ends a line, "\f" ends a page. */
static int lw_text(const MemIo *io, LwBuf *out) {
    char *buf = NULL;
    size_t len = 0;
    memset(out, 0, sizeof *out);
    if (io->buf == NULL || pdf_inflate_copy(io->buf, io->len, &buf, &len, NULL) != 0) {
        return -1;
    }
    static const char page[] = "BT\n/F1 ";
    static const char span[] = "/ActualText <FEFF";
    int pages = 0;
    int rc = lw_put(out, "", 0);
    for (size_t i = 0; rc == 0 && i < len; i++) {
        if (i + sizeof page - 1 <= len && memcmp(buf + i, page, sizeof page - 1) == 0) {
            if (pages++ > 0) {
                rc = lw_put(out, "\f", 1);
            }
            continue;
        }
        if (i + sizeof span - 1 > len || memcmp(buf + i, span, sizeof span - 1) != 0) {
            continue;
        }
        i += sizeof span - 1;
        unsigned hi = 0;
        while (rc == 0 && i + 4 <= len && buf[i] != '>') {
            char hex[5] = {buf[i], buf[i + 1], buf[i + 2], buf[i + 3], 0};
            unsigned u = (unsigned)strtoul(hex, NULL, 16);
            i += 4;
            if (u >= 0xD800 && u < 0xDC00) {
                hi = u;
                continue;
            }
            if (hi != 0) {
                u = 0x10000 + ((hi - 0xD800) << 10) + (u - 0xDC00);
                hi = 0;
            }
            rc = lw_utf8(out, u);
        }
        if (rc == 0) {
            rc = lw_put(out, "\n", 1);
        }
    }
    if (rc == 0 && pages > 0) {
        rc = lw_put(out, "\f", 1);
    }
    free(buf);
    return rc;
}

/* Custom mapper: TEXT goes to ki_doc_text in pieces of `piece` bytes (0 is
   whole), <br/> calls ki_doc_break, and after `cancel_after` TEXT events the
   cancel callback starts returning true. */
typedef struct {
    size_t piece;
    int texts;
    int cancel_after;
    int breaks;
} LwMap;

static int lw_map(void *user, KiDoc *doc, const KiEvent *ev) {
    LwMap *m = user;
    if (ev->kind == KI_EV_START && strcmp(ev->name, "br") == 0) {
        m->breaks++;
        return ki_doc_break(doc);
    }
    if (ev->kind != KI_EV_TEXT) {
        return 0;
    }
    size_t step = m->piece > 0 ? m->piece : ev->text_len;
    for (size_t i = 0; i < ev->text_len; i += step) {
        size_t k = ev->text_len - i < step ? ev->text_len - i : step;
        if (ki_doc_text(doc, ev->text + i, k) != 0) {
            return -1;
        }
    }
    m->texts++;
    return 0;
}

typedef struct {
    MemIo io;
    LwMap *map;
} LwIo;

static int lw_read(void *user, char *dst, int max) {
    return mem_read(&((LwIo *)user)->io, dst, max);
}

static int lw_write(void *user, const char *src, int n) {
    return mem_write(&((LwIo *)user)->io, src, n);
}

static int lw_cancelled(void *user) {
    LwIo *w = user;
    return w->map->cancel_after > 0 && w->map->texts >= w->map->cancel_after;
}

/* Converts `xml` through lw_map with read size `chunk`. On success out holds
   the extracted lines. */
static int lw_run_with(const char *xml, size_t n, int chunk, LwMap *map, LwIo *w, int max_pages, KiResult *res,
                       LwBuf *out) {
    memset(w, 0, sizeof *w);
    memset(out, 0, sizeof *out);
    w->io.s = xml;
    w->io.n = n;
    w->io.chunk = chunk;
    w->map = map;
    if (lw_font() != 0) {
        return -100;
    }
    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = lw_font_path;
    opt.temp_dir = "/tmp";
    opt.max_pages = max_pages;
    opt.map = lw_map;
    opt.map_user = map;
    opt.cancelled = lw_cancelled;
    opt.cancel_user = w;
    opt.read_bytes = lw_read;
    opt.write_bytes = lw_write;
    opt.io_user = w;
    int st = ki_convert(&opt, res);
    if (st == XML2PDF_OK && lw_text(&w->io, out) != 0) {
        st = -101;
    }
    free(w->io.buf);
    w->io.buf = NULL;
    return st;
}

static int lw_run(const char *xml, int chunk, size_t piece, KiResult *res, LwBuf *out) {
    LwMap map = {piece, 0, 0, 0};
    LwIo w;
    return lw_run_with(xml, strlen(xml), chunk, &map, &w, 0, res, out);
}

/* Wraps body in <A>...</A> and checks every read size/piece schedule gives
   exactly `want`. */
static int lw_expect(const char *body, const char *want) {
    LwBuf xml = {0};
    if (lw_puts(&xml, "<A>") != 0 || lw_puts(&xml, body) != 0 || lw_puts(&xml, "</A>") != 0) {
        lw_free(&xml);
        return 0;
    }
    static const int chunks[] = {0, 1, 3, 4096};
    static const size_t pieces[] = {0, 1, 2, 3, 7};
    int ok = 1;
    for (size_t c = 0; ok && c < sizeof chunks / sizeof chunks[0]; c++) {
        for (size_t p = 0; ok && p < sizeof pieces / sizeof pieces[0]; p++) {
            KiResult res;
            LwBuf got;
            int st = lw_run(xml.s, chunks[c], pieces[p], &res, &got);
            if (st != XML2PDF_OK || got.s == NULL || strcmp(got.s, want) != 0) {
                fprintf(stderr, "lw_expect chunk=%d piece=%zu status=%d (%s)\n  got  [%s]\n  want [%s]\n",
                        chunks[c], pieces[p], st, res.message, got.s != NULL ? got.s : "", want);
                ok = 0;
            }
            lw_free(&got);
        }
    }
    lw_free(&xml);
    return ok;
}

#define A10 "AAAAAAAAAA"
#define NBSP "\xC2\xA0"
#define ZHE "\xD0\x96"
#define I_UA "\xD1\x96"
#define ACUTE "\xCC\x81"

static int test_wrap_opportunities(void) {
    CHECK(lw_font() == 0);
    /* 10 x 5320 = 53200 is exactly the line: no empty first line. */
    CHECK(lw_expect(A10, A10 "\n\f"));
    CHECK(lw_expect(A10 " A", A10 " \nA\n\f"));
    /* One positive unit over (5321) inside a word: fallback before B. */
    CHECK(lw_expect("AAAAAAAAAB", "AAAAAAAAA\nB\n\f"));
    /* Two words: the second does not fit, so the break is the last space. */
    CHECK(lw_expect("AAAAA AAAAB", "AAAAA \nAAAAB\n\f"));
    CHECK(lw_expect("AAAAA AAAA", "AAAAA AAAA\n\f"));
    CHECK(lw_expect("AAA AAA AAA AAA", "AAA AAA AAA \nAAA\n\f"));
    /* Tab is an opportunity measured like a space. */
    CHECK(lw_expect("AAAAA\tAAAAA", "AAAAA\t\nAAAAA\n\f"));
    CHECK(lw_expect("AA\tAA", "AA\tAA\n\f"));
    /* NBSP joins its neighbours into one word and is kept as U+00A0. */
    CHECK(lw_expect("AA AAAAA" NBSP "AAA", "AA \nAAAAA" NBSP "AAA\n\f"));
    CHECK(lw_expect("AAAAA" NBSP "AAAAA", "AAAAA" NBSP "AAAA\nA\n\f"));
    /* Spaces after a full line stay with it; the next word starts the next. */
    CHECK(lw_expect(A10 "   " A10, A10 "   \n" A10 "\n\f"));
    return 0;
}

static int test_wrap_oversized(void) {
    LwBuf body = {0}, want = {0};
    /* ASCII and Cyrillic tokens over the width split at code points. */
    CHECK(lw_rep(&body, "A", 25) == 0);
    CHECK(lw_expect(body.s, A10 "\n" A10 "\nAAAAA\n\f"));
    lw_free(&body);
    CHECK(lw_puts(&body, "AA ") == 0 && lw_rep(&body, "A", 25) == 0);
    CHECK(lw_expect(body.s, "AA \n" A10 "\n" A10 "\nAAAAA\n\f"));
    lw_free(&body);
    CHECK(lw_rep(&body, ZHE, 12) == 0);
    CHECK(lw_rep(&want, ZHE, 10) == 0 && lw_puts(&want, "\n" ZHE ZHE "\n\f") == 0);
    CHECK(lw_expect(body.s, want.s));
    lw_free(&body);
    lw_free(&want);
    /* 600 narrow two-byte letters fit the width but not 1023 bytes: 511 + 89. */
    CHECK(lw_rep(&body, I_UA, 600) == 0);
    CHECK(lw_rep(&want, I_UA, 511) == 0 && lw_puts(&want, "\n") == 0 && lw_rep(&want, I_UA, 89) == 0 &&
          lw_puts(&want, "\n\f") == 0);
    CHECK(lw_expect(body.s, want.s));
    lw_free(&body);
    lw_free(&want);
    /* A trailing zero-advance mark stays with its base across the split. */
    CHECK(lw_expect(A10 ACUTE "A", A10 ACUTE "\nA\n\f"));
    return 0;
}

static int test_wrap_zero_advance(void) {
    LwBuf body = {0}, want = {0}, word = {0};
    /* Three 601-byte words: each fits the width, two do not fit 1023 bytes. */
    CHECK(lw_puts(&word, "A") == 0 && lw_rep(&word, ACUTE, 300) == 0);
    for (int i = 0; i < 3; i++) {
        CHECK(lw_puts(&body, word.s) == 0 && lw_puts(&body, i < 2 ? " " : "") == 0);
        CHECK(lw_puts(&want, word.s) == 0 && lw_puts(&want, i < 2 ? " \n" : "\n\f") == 0);
    }
    CHECK(lw_expect(body.s, want.s));
    lw_free(&body);
    lw_free(&want);
    lw_free(&word);
    /* One 1602-byte word splits before its second base letter. */
    CHECK(lw_puts(&word, "A") == 0 && lw_rep(&word, ACUTE, 400) == 0);
    CHECK(lw_puts(&body, word.s) == 0 && lw_puts(&body, word.s) == 0);
    CHECK(lw_puts(&want, word.s) == 0 && lw_puts(&want, "\n") == 0 && lw_puts(&want, word.s) == 0 &&
          lw_puts(&want, "\n\f") == 0);
    CHECK(lw_expect(body.s, want.s));
    lw_free(&body);
    lw_free(&want);
    lw_free(&word);
    /* A base with 511 marks is the 1023-byte cap; 512 marks cannot split. */
    KiResult res;
    LwBuf got;
    CHECK(lw_puts(&body, "<A>A") == 0 && lw_rep(&body, ACUTE, 511) == 0 && lw_puts(&body, "</A>") == 0);
    CHECK(lw_run(body.s, 1, 0, &res, &got) == XML2PDF_OK);
    lw_free(&got);
    lw_free(&body);
    CHECK(lw_puts(&body, "<A>A") == 0 && lw_rep(&body, ACUTE, 512) == 0 && lw_puts(&body, "</A>") == 0);
    CHECK(lw_run(body.s, 1, 1, &res, &got) == XML2PDF_ERR_RESOURCE);
    CHECK(res.cause == KI_CAUSE_RESOURCE && strstr(res.message, "1023") != NULL);
    lw_free(&got);
    lw_free(&body);
    return 0;
}

static int test_wrap_fragments(void) {
    /* 22610 + 6650 + 1330 + 10640 + 1330 = 41230 units; five Zhe (26600)
       do not fit after it, and the 12 A token does not fit after them. */
    const char *want = "AAAA " NBSP ZHE "\tAA \n" ZHE ZHE ZHE ZHE ZHE " \n" A10 "\nAA" ACUTE " B\n\f";
    const char *forms[] = {
        "AAAA " NBSP ZHE "\tAA " ZHE ZHE ZHE ZHE ZHE " AAAAAAAAAAAA" ACUTE " B",
        "AA<!---->AA " NBSP "<!---->" ZHE "\tAA " ZHE "<!---->" ZHE ZHE ZHE ZHE " AAAAA<!---->AAAAAAA" ACUTE
        "<!----> B",
        "<![CDATA[AAAA " NBSP ZHE "\t]]>AA " ZHE ZHE "<![CDATA[" ZHE ZHE ZHE " AAAAAAAAAAAA]]>" ACUTE " B",
        "AAAA &#xA0;&#x416;&#9;AA &#1046;" ZHE ZHE ZHE ZHE " AAAAAAAAAAAA&#x301; B",
    };
    for (size_t i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        CHECK(lw_expect(forms[i], want));
    }
    /* A break flushes a pending word once; consecutive breaks add nothing. */
    CHECK(lw_expect("AA<br/>BB", "AA\nBB\n\f"));
    CHECK(lw_expect("AA<br/><br/><br/>BB<br/>", "AA\nBB\n\f"));
    CHECK(lw_expect("AA <br/>B", "AA \nB\n\f"));
    /* LF inside text always ends a line, including an empty one. */
    CHECK(lw_expect("AA\n\nBB", "AA\n\nBB\n\f"));
    return 0;
}

/* `words` words of A10, one per line; returns the expected text. */
static int lw_words(LwBuf *body, LwBuf *want, int words, const char *sep) {
    for (int i = 0; i < words; i++) {
        if (lw_puts(body, A10) != 0 || (i + 1 < words && lw_puts(body, sep) != 0)) {
            return -1;
        }
        if (lw_puts(want, A10) != 0 || (i + 1 < words && lw_puts(want, " ") != 0) || lw_puts(want, "\n") != 0) {
            return -1;
        }
        if ((i + 1) % 50 == 0 || i + 1 == words) {
            if (lw_puts(want, "\f") != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int test_wrap_pages(void) {
    LwBuf body = {0}, want = {0}, xml = {0}, got;
    KiResult res;
    LwMap map;
    LwIo w;
    /* Exactly 50 lines fill one page; the 51st (a pending final word) opens a
       second page without an empty one in between. */
    CHECK(lw_words(&body, &want, 50, " ") == 0);
    CHECK(lw_expect(body.s, want.s));
    lw_free(&body);
    lw_free(&want);
    CHECK(lw_words(&body, &want, 51, " ") == 0);
    CHECK(lw_expect(body.s, want.s));
    /* The same words in separate text events. */
    LwBuf ev = {0}, evwant = {0};
    CHECK(lw_words(&ev, &evwant, 51, "<!----> ") == 0);
    CHECK(strcmp(evwant.s, want.s) == 0 && lw_expect(ev.s, want.s));
    lw_free(&ev);
    lw_free(&evwant);

    /* Page quota: exactly two pages succeed, one fails RESOURCE. */
    CHECK(lw_puts(&xml, "<A>") == 0 && lw_puts(&xml, body.s) == 0 && lw_puts(&xml, "</A>") == 0);
    memset(&map, 0, sizeof map);
    CHECK(lw_run_with(xml.s, xml.n, 7, &map, &w, 2, &res, &got) == XML2PDF_OK && res.pages == 2);
    CHECK(strcmp(got.s, want.s) == 0);
    lw_free(&got);
    memset(&map, 0, sizeof map);
    CHECK(lw_run_with(xml.s, xml.n, 7, &map, &w, 1, &res, &got) == XML2PDF_ERR_RESOURCE);
    CHECK(res.cause == KI_CAUSE_RESOURCE);
    lw_free(&got);
    lw_free(&xml);
    lw_free(&body);
    lw_free(&want);

    /* An oversized token across the page transition: 50 full lines + 5. */
    CHECK(lw_rep(&body, "A", 505) == 0);
    CHECK(lw_rep(&want, A10 "\n", 50) == 0 && lw_puts(&want, "\fAAAAA\n\f") == 0);
    CHECK(lw_expect(body.s, want.s));
    lw_free(&body);
    lw_free(&want);

    /* Cancellation with a pending word, and right after a page was written. */
    const char *pending = "<A>AA<!---->AA</A>";
    memset(&map, 0, sizeof map);
    map.cancel_after = 1;
    CHECK(lw_run_with(pending, strlen(pending), 0, &map, &w, 0, &res, &got) == XML2PDF_ERR_RESOURCE);
    CHECK(res.cause == KI_CAUSE_CANCEL && map.texts == 1);
    lw_free(&got);
    /* Word k arrives in event k and is placed by event k + 1, so event 53
       commits line 51 and writes page 1 first. */
    CHECK(lw_words(&body, &want, 120, "<!----> ") == 0);
    CHECK(lw_puts(&xml, "<A>") == 0 && lw_puts(&xml, body.s) == 0 && lw_puts(&xml, "</A>") == 0);
    for (int after = 52; after <= 53; after++) {
        memset(&map, 0, sizeof map);
        map.cancel_after = after;
        CHECK(lw_run_with(xml.s, xml.n, 0, &map, &w, 0, &res, &got) == XML2PDF_ERR_RESOURCE);
        CHECK(res.cause == KI_CAUSE_CANCEL && map.texts == after && res.pages == after - 52);
        lw_free(&got);
    }
    lw_free(&xml);
    lw_free(&body);
    lw_free(&want);

    /* A character outside the font inside a pending word stays a FONT error. */
    CHECK(lw_run("<A>AA A\xE2\x82\xAC</A>", 0, 0, &res, &got) == XML2PDF_ERR_XML);
    CHECK(res.cause == KI_CAUSE_FONT && strstr(res.message, "U+20AC") != NULL);
    lw_free(&got);
    return 0;
}

/* Streams "<A>" + n x 'A' + "</A>" without holding the input. */
typedef struct {
    size_t n;
    size_t i;
    long out;
    int with_text;
    MemIo text;
} LwGen;

static int lw_gen_read(void *user, char *dst, int max) {
    LwGen *g = user;
    size_t total = g->n + 7;
    int k = 0;
    while (k < max && g->i < total) {
        size_t at = g->i++;
        dst[k++] = at < 3 ? "<A>"[at] : at < 3 + g->n ? 'A' : "</A>"[at - 3 - g->n];
    }
    return k;
}

static int lw_gen_write(void *user, const char *src, int n) {
    LwGen *g = user;
    g->out += n;
    return g->with_text ? mem_write(&g->text, src, n) : n;
}

static int lw_gen_convert(LwGen *g, size_t n, int with_text, KiResult *res) {
    memset(g, 0, sizeof *g);
    g->n = n;
    g->with_text = with_text;
    LwMap map = {0, 0, 0, 0};
    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = lw_font_path;
    opt.temp_dir = "/tmp";
    opt.map = lw_map;
    opt.map_user = &map;
    opt.read_bytes = lw_gen_read;
    opt.write_bytes = lw_gen_write;
    opt.io_user = g;
    return ki_convert(&opt, res);
}

/* Peak RSS growth in KiB of a forked child converting an n-byte token, or
   -1. The child checks the page count and reports through a pipe. */
static long lw_child_rss(size_t n, int pages) {
    int fd[2];
    if (pipe(fd) != 0) {
        return -1;
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(fd[0]);
        close(fd[1]);
        return -1;
    }
    if (pid == 0) {
        close(fd[0]);
        struct rusage r0, r1;
        LwGen g;
        KiResult res;
        long delta = -1;
        if (getrusage(RUSAGE_SELF, &r0) == 0) {
            int st = lw_gen_convert(&g, n, 0, &res);
            if (st == XML2PDF_OK && res.pages == pages && g.out > 0 && getrusage(RUSAGE_SELF, &r1) == 0) {
                delta = r1.ru_maxrss - r0.ru_maxrss;
            }
        }
        _exit(write(fd[1], &delta, sizeof delta) == (ssize_t)sizeof delta ? 0 : 1);
    }
    close(fd[1]);
    long delta = -1;
    if (read(fd[0], &delta, sizeof delta) != (ssize_t)sizeof delta) {
        delta = -1;
    }
    close(fd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return delta;
}

static int lw_check_token(size_t n) {
    LwGen g;
    KiResult res;
    size_t lines = (n + 9) / 10;
    int pages = (int)((lines + 49) / 50);
    CHECK(lw_gen_convert(&g, n, 1, &res) == XML2PDF_OK && res.pages == pages);
    LwBuf got;
    CHECK(lw_text(&g.text, &got) == 0);
    free(g.text.buf);
    /* Every line but the last is A10; the last holds the remainder. */
    size_t at = 0, seen = 0;
    for (size_t l = 0; l < lines; l++) {
        size_t k = l + 1 < lines || n % 10 == 0 ? 10 : n % 10;
        for (size_t j = 0; j < k; j++) {
            CHECK(at < got.n && got.s[at++] == 'A');
        }
        seen += k;
        CHECK(at < got.n && got.s[at++] == '\n');
        if ((l + 1) % 50 == 0 || l + 1 == lines) {
            CHECK(at < got.n && got.s[at++] == '\f');
        }
    }
    CHECK(at == got.n && seen == n);
    lw_free(&got);
    return 0;
}

static int test_wrap_token_memory(void) {
    CHECK(lw_font() == 0);
    /* The pending word shares the line buffer: no token-sized storage. */
    Layout probe;
    CHECK(sizeof probe.line == LAYOUT_LINE && sizeof probe.page == LAYOUT_LINES * LAYOUT_LINE);
    CHECK(sizeof(Layout) < LAYOUT_LINES * LAYOUT_LINE + 2 * LAYOUT_LINE + 1024);
    const size_t small = 64 * 1024, big = 4 * 1024 * 1024;
    CHECK(lw_check_token(small) == 0);
    CHECK(lw_check_token(big) == 0);
    long rss_small = lw_child_rss(small, (int)(((small + 9) / 10 + 49) / 50));
    long rss_big = lw_child_rss(big, (int)(((big + 9) / 10 + 49) / 50));
    printf("wrap-token small=%zu big=%zu rss_growth_small_kb=%ld rss_growth_big_kb=%ld layout_bytes=%zu\n", small,
           big, rss_small, rss_big, sizeof(Layout));
    CHECK(rss_small >= 0 && rss_big >= 0);
#if KI_RSS_GATE
    CHECK(rss_big - rss_small < 2048);
#endif
    return 0;
}

static int test_wrap_profiles(void) {
    MemIo io;
    KiResult res;
    LwBuf got;
    /* records-v1: whitespace with a newline after a pending word is not
       indentation, so it still ends the line. */
    const char *pend = "<doc>AB<!---->\n<!---->CD</doc>";
    CHECK(fm_convert(pend, strlen(pend), 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(lw_text(&io, &got) == 0 && strcmp(got.s, "doc\nAB\nCD\n\f") == 0);
    free(io.buf);
    lw_free(&got);
    const char *indent = "<doc>\n  <p>A B</p>\n  <p xml:space=\"preserve\">\n  x</p></doc>";
    CHECK(fm_convert(indent, strlen(indent), 1, "fonts/DejaVuSans.ttf", &io, &res) == XML2PDF_OK);
    CHECK(lw_text(&io, &got) == 0 && strcmp(got.s, "doc\np\nA B\np\nxml:space: preserve\n\n  x\n\f") == 0);
    free(io.buf);
    lw_free(&got);

    /* Both profiles produce the same PDF for whole and one-byte reads,
       for text, CDATA and references. */
    LwBuf a = {0}, b = {0};
    CHECK(lw_puts(&a, "<doc><p>") == 0 && lw_rep(&a, "word \xD1\x81\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xBE ", 40) == 0 &&
          lw_rep(&a, "x", 300) == 0 && lw_puts(&a, "</p></doc>") == 0);
    CHECK(lw_puts(&b, "<doc><p>") == 0 &&
          lw_rep(&b, "wo<![CDATA[rd \xD1\x81]]>&#x43B;\xD0\xBE\xD0\xB2<!---->\xD0\xBE&#32;", 40) == 0 &&
          lw_rep(&b, "x", 300) == 0 && lw_puts(&b, "</p></doc>") == 0);
    CHECK(fm_same_pdf(a.s, a.n, a.s, a.n, 1));
    CHECK(fm_same_pdf(a.s, a.n, b.s, b.n, 1));
    lw_free(&a);
    lw_free(&b);

    const char *rep_a = "<report version=\"1\" title=\"T\"><record><field name=\"n\">alpha beta gamma delta "
                        "epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi "
                        "chi psi omega</field></record></report>";
    MemIo x, y;
    KiResult rx, ry;
    KiOptions opt;
    for (int pass = 0; pass < 2; pass++) {
        MemIo *m = pass == 0 ? &x : &y;
        memset(m, 0, sizeof *m);
        m->s = rep_a;
        m->n = strlen(rep_a);
        m->chunk = pass == 0 ? 0 : 1;
        ki_options_init(&opt);
        opt.font_path = "fonts/DejaVuSans.ttf";
        opt.profile = KI_PROFILE_REPORT;
        opt.read_bytes = mem_read;
        opt.write_bytes = mem_write;
        opt.io_user = m;
        CHECK(ki_convert(&opt, pass == 0 ? &rx : &ry) == XML2PDF_OK);
    }
    CHECK(x.len == y.len && memcmp(x.buf, y.buf, x.len) == 0);
    CHECK(lw_text(&x, &got) == 0);
    /* The field wraps only after spaces, and its lines rejoin to the value. */
    const char *value = strstr(rep_a, "alpha");
    size_t value_n = (size_t)(strstr(rep_a, "</field>") - value);
    char *l = strstr(got.s, "n: alpha");
    CHECK(l != NULL);
    l += 3;
    LwBuf joined = {0};
    int lines = 0;
    while (joined.n < value_n) {
        char *e = strchr(l, '\n');
        CHECK(e != NULL && e > l);
        CHECK(lw_put(&joined, l, (size_t)(e - l)) == 0);
        CHECK(joined.n >= value_n || e[-1] == ' ');
        lines++;
        l = e + 1;
    }
    CHECK(lines >= 2 && joined.n == value_n && memcmp(joined.s, value, value_n) == 0);
    lw_free(&joined);
    free(x.buf);
    free(y.buf);
    lw_free(&got);
    return 0;
}
