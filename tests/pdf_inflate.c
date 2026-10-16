/* Independent reader for the writer's stream objects. Included early by
   test_stream.c. It locates objects through the classic xref, not through
   writer-side knowledge, and checks each stream against its declared
   length before anything else looks at the content. */

#define PDF_INFLATE_MAX_STREAMS 4096

typedef struct {
    int obj;
    int flate;
    size_t declared;   /* /Length value, direct or through the referenced object */
    size_t inflated;   /* decoded size (equals declared when not compressed) */
    long length1;      /* /Length1, or -1 */
    size_t out_at;     /* decoded bytes start here in the expanded copy */
} PdfStreamInfo;

typedef struct {
    PdfStreamInfo s[PDF_INFLATE_MAX_STREAMS];
    int n;
    int nflate;
} PdfStreamList;

static const char *pi_find(const char *hay, size_t n, const char *needle) {
    size_t m = strlen(needle);
    for (size_t i = 0; i + m <= n; i++) {
        if (memcmp(hay + i, needle, m) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

/* Offset of "num 0 obj\n" from the xref table, or -1. */
static long pi_obj_offset(const char *pdf, size_t n, int num) {
    const char *at = NULL;
    for (size_t i = 0; i + 10 <= n; i++) {
        if (memcmp(pdf + i, "startxref\n", 10) == 0) {
            at = pdf + i;
        }
    }
    if (at == NULL) {
        return -1;
    }
    long xref = strtol(at + 10, NULL, 10);
    if (xref <= 0 || (size_t)xref + 5 >= n || memcmp(pdf + xref, "xref\n0 ", 7) != 0) {
        return -1;
    }
    int count = atoi(pdf + xref + 7);
    const char *nl = memchr(pdf + xref + 7, '\n', n - (size_t)xref - 7);
    if (nl == NULL || num <= 0 || num >= count) {
        return -1;
    }
    const char *e = nl + 1 + 20 * (size_t)num;
    if (e + 20 > pdf + n || e[17] != 'n') {
        return -1;
    }
    long off = strtol(e, NULL, 10);
    char head[32];
    int hl = snprintf(head, sizeof head, "%d 0 obj\n", num);
    if (off <= 0 || (size_t)off + (size_t)hl > n || memcmp(pdf + off, head, (size_t)hl) != 0) {
        return -1;
    }
    return off;
}

static int pi_append(char **out, size_t *len, size_t *cap, const void *src, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t c = *cap == 0 ? 65536 : *cap;
        while (c < *len + n + 1) {
            c *= 2;
        }
        char *g = realloc(*out, c);
        if (g == NULL) {
            return -1;
        }
        *out = g;
        *cap = c;
    }
    memcpy(*out + *len, src, n);
    *len += n;
    (*out)[*len] = '\0';
    return 0;
}

static int pi_inflate(const char *src, size_t n, char **out, size_t *len, size_t *cap, size_t *got) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit(&zs) != Z_OK) {
        return -1;
    }
    zs.next_in = (Bytef *)src;
    zs.avail_in = (uInt)n;
    unsigned char chunk[16384];
    size_t start = *len;
    int rc;
    do {
        zs.next_out = chunk;
        zs.avail_out = sizeof chunk;
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            return -1;
        }
        if (pi_append(out, len, cap, chunk, sizeof chunk - zs.avail_out) != 0) {
            inflateEnd(&zs);
            return -1;
        }
    } while (rc != Z_STREAM_END && (zs.avail_in > 0 || zs.avail_out == 0));
    int ok = rc == Z_STREAM_END && zs.avail_in == 0 && zs.total_in == n;
    inflateEnd(&zs);
    *got = *len - start;
    return ok ? 0 : -1;
}

/* Copies pdf with each FlateDecode stream body replaced by its decoded
   bytes. Fails on a missing or wrong /Length, bytes after the deflate end,
   a damaged or truncated deflate body, or a /Length1 differing from the
   decoded size. list may be NULL. The copy's xref offsets are not valid. */
static int pdf_inflate_copy(const char *pdf, size_t n, char **outp, size_t *outn, PdfStreamList *list) {
    char *out = NULL;
    size_t len = 0, cap = 0, pos = 0;
    if (list != NULL) {
        memset(list, 0, sizeof *list);
    }
    for (;;) {
        const char *s = pi_find(pdf + pos, n - pos, ">>\nstream\n");
        if (s == NULL) {
            break;
        }
        size_t dict_end = (size_t)(s - pdf);
        size_t body = dict_end + 10;
        size_t obj_at = dict_end;
        while (obj_at > pos && !(obj_at >= 7 && memcmp(pdf + obj_at - 6, " 0 obj", 6) == 0)) {
            obj_at--;
        }
        size_t line = obj_at;
        while (line > 0 && pdf[line - 1] != '\n') {
            line--;
        }
        int obj = atoi(pdf + line);
        size_t dl = dict_end - obj_at;
        char dict[512];
        if (obj <= 0 || dl >= sizeof dict) {
            goto fail;
        }
        memcpy(dict, pdf + obj_at, dl);
        dict[dl] = '\0';
        int flate = strstr(dict, "/Filter /FlateDecode") != NULL;
        const char *lk = strstr(dict, "/Length ");
        if (lk == NULL) {
            goto fail;
        }
        char *end = NULL;
        long v = strtol(lk + 8, &end, 10);
        long declared = -1;
        if (strncmp(end, " 0 R", 4) == 0) {
            long off = pi_obj_offset(pdf, n, (int)v);
            if (off < 0) {
                goto fail;
            }
            const char *nl = memchr(pdf + off, '\n', n - (size_t)off);
            char *vend = NULL;
            declared = nl != NULL ? strtol(nl + 1, &vend, 10) : -1;
            if (vend == NULL || strncmp(vend, "\nendobj\n", 8) != 0) {
                goto fail;
            }
        } else {
            declared = v;
        }
        long length1 = -1;
        const char *l1 = strstr(dict, "/Length1 ");
        if (l1 != NULL) {
            length1 = strtol(l1 + 9, NULL, 10);
        }
        if (declared < 0 || body + (size_t)declared + 10 > n ||
            memcmp(pdf + body + (size_t)declared, "\nendstream", 10) != 0) {
            goto fail;
        }
        if (pi_append(&out, &len, &cap, pdf + pos, body - pos) != 0) {
            goto fail;
        }
        size_t out_at = len, got = 0;
        if (flate) {
            if (pi_inflate(pdf + body, (size_t)declared, &out, &len, &cap, &got) != 0) {
                goto fail;
            }
        } else {
            if (pi_append(&out, &len, &cap, pdf + body, (size_t)declared) != 0) {
                goto fail;
            }
            got = (size_t)declared;
        }
        if (length1 >= 0 && (size_t)length1 != got) {
            goto fail;
        }
        if (list != NULL && list->n < PDF_INFLATE_MAX_STREAMS) {
            PdfStreamInfo *e = &list->s[list->n++];
            e->obj = obj;
            e->flate = flate;
            e->declared = (size_t)declared;
            e->inflated = got;
            e->length1 = length1;
            e->out_at = out_at;
            list->nflate += flate;
        }
        pos = body + (size_t)declared;
    }
    if (pi_append(&out, &len, &cap, pdf + pos, n - pos) != 0) {
        goto fail;
    }
    *outp = out;
    *outn = len;
    return 0;
fail:
    free(out);
    return -1;
}
