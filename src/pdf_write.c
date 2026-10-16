#define _GNU_SOURCE

#include "pdf_write.h"

#include "font.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Objects 1 and 2 are written at the end. Font objects are a fixed block.
   Each page then uses two object numbers, so page ids are not stored. */
#define OBJ_CATALOG 1
#define OBJ_PAGES 2
#define OBJ_TYPE0 3
#define OBJ_CID 4
#define OBJ_DESC 5
#define OBJ_FILE 6
#define OBJ_CMAP 7
#define OBJ_FIRST 8
#define MAX_OBJECTS 8000000
#define PAGE_FONT_PT 10
#define PAGE_LEADING 14
#define PAGE_MARGIN 40
#define PAGE_WIDTH 612
#define PAGE_TOP 750
#define PAGE_MAX_LINES 50

static int note_offset(PdfWriter *w, int num) {
    if (num <= 0 || num >= MAX_OBJECTS) {
        w->hit_limit = 1;
        w->fail_kind = 1;
        return -1;
    }
    long spool = (long)(num + 1) * 8;
    if (w->max_spool > 0 && spool > w->max_spool) {
        w->hit_limit = 1;
        w->fail_kind = 5;
        return -1;
    }
    if (fflush(w->fp) != 0) {
        w->fail_kind = 2;
        return -1;
    }
    long off = w->written;
    if (fseek(w->offsets, (long)num * 8, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(&off, sizeof off, 1, w->offsets) != 1) {
        return -1;
    }
    if (num > w->nobj) {
        w->nobj = num;
    }
    return 0;
}

static int begin_obj(PdfWriter *w, int num) {
    if (note_offset(w, num) != 0) {
        return -1;
    }
    if (fprintf(w->fp, "%d 0 obj\n", num) < 0) {
        return -1;
    }
    return 0;
}

static int write_stream_obj(PdfWriter *w, int num, const char *data, size_t len) {
    if (begin_obj(w, num) != 0) {
        return -1;
    }
    if (fprintf(w->fp, "<< /Length %zu >>\nstream\n", len) < 0) {
        return -1;
    }
    if (fwrite(data, 1, len, w->fp) != len) {
        return -1;
    }
    if (fputs("\nendstream\nendobj\n", w->fp) < 0) {
        return -1;
    }
    return 0;
}

static int pdf_scale(const FontFace *face, int value) {
    return value * 1000 / face->units;
}

/* Bytes outside the PDF regular-character set, and '#', become #XX, so a
   font name can never end or extend the name token. */
static void pdf_name(const char *raw, char *out) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)raw; *p != '\0'; p++) {
        if (*p > 0x20 && *p < 0x7F && strchr("#()<>[]{}/%", *p) == NULL) {
            out[o++] = (char)*p;
        } else {
            o += (size_t)sprintf(out + o, "#%02X", *p);
        }
    }
    out[o] = '\0';
}

static int write_tounicode(PdfWriter *w, const FontFace *face) {
    size_t cap = 256 * 1024;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return -1;
    }
    size_t o = 0;
    const char *head =
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    size_t hlen = strlen(head);
    if (hlen >= cap) {
        free(buf);
        return -1;
    }
    memcpy(buf, head, hlen);
    o = hlen;

    int in_group = 0;
    int count = 0;
    size_t count_at = 0;
    unsigned cps[FONT_BMP_MAX + FONT_PUNCT_COUNT];
    int ncp = 0;
    /* A GID shared by several code points maps back to the lowest one only,
       so every glyph has exactly one Unicode value. */
    unsigned char *seen = calloc(65536 / 8, 1);
    if (seen == NULL) {
        free(buf);
        return -1;
    }
    for (unsigned k = 0; k < FONT_BMP_MAX + FONT_PUNCT_COUNT; k++) {
        unsigned cp = k < FONT_BMP_MAX ? k : FONT_PUNCT_BASE + (k - FONT_BMP_MAX);
        unsigned g = k < FONT_BMP_MAX ? face->gid[k] : face->punct_gid[k - FONT_BMP_MAX];
        if (g != 0 && !(seen[g / 8] & (1u << (g % 8)))) {
            seen[g / 8] |= (unsigned char)(1u << (g % 8));
            cps[ncp++] = cp;
        }
    }
    free(seen);
    for (int i = 0; i < ncp; i++) {
        unsigned cp = cps[i];
        uint16_t gid = 0;
        uint16_t adv = 0;
        if (font_lookup(face, cp, &gid, &adv) != 0) {
            continue;
        }
        if (count == 0) {
            count_at = o;
            if (o + 32 >= cap) {
                free(buf);
                return -1;
            }
            memcpy(buf + o, "000 beginbfchar\n", 16);
            o += 16;
            in_group = 1;
        }
        if (o + 24 >= cap) {
            free(buf);
            return -1;
        }
        o += (size_t)sprintf(buf + o, "<%04X> <%04X>\n", gid, cp);
        count++;
        if (count == 100 || i + 1 == ncp) {
            char num[4];
            if (count < 1 || count > 100) {
                free(buf);
                return -1;
            }
            num[0] = (char)('0' + count / 100);
            num[1] = (char)('0' + (count / 10) % 10);
            num[2] = (char)('0' + count % 10);
            num[3] = '\0';
            memcpy(buf + count_at, num, 3);
            if (o + 16 >= cap) {
                free(buf);
                return -1;
            }
            memcpy(buf + o, "endbfchar\n", 10);
            o += 10;
            count = 0;
            in_group = 0;
        }
    }
    if (in_group) {
        free(buf);
        return -1;
    }
    const char *tail = "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    size_t tlen = strlen(tail);
    if (o + tlen >= cap) {
        free(buf);
        return -1;
    }
    memcpy(buf + o, tail, tlen);
    o += tlen;
    int rc = write_stream_obj(w, OBJ_CMAP, buf, o);
    free(buf);
    return rc;
}

static int write_widths(FILE *fp, const FontFace *face) {
    if (fputs("/W [\n", fp) < 0) {
        return -1;
    }
    for (unsigned cp = 0; cp < FONT_BMP_MAX; cp++) {
        if (face->gid[cp] == 0) {
            continue;
        }
        int width = pdf_scale(face, face->adv[cp]);
        if (fprintf(fp, "%u [%d]\n", face->gid[cp], width) < 0) {
            return -1;
        }
    }
    for (unsigned i = 0; i < FONT_PUNCT_COUNT; i++) {
        if (face->punct_gid[i] == 0) {
            continue;
        }
        int width = pdf_scale(face, face->punct_adv[i]);
        if (fprintf(fp, "%u [%d]\n", face->punct_gid[i], width) < 0) {
            return -1;
        }
    }
    if (fputs("]\n", fp) < 0) {
        return -1;
    }
    return 0;
}

static int write_font_objects(PdfWriter *w) {
    FontFace *face = &w->font;
    if (write_stream_obj(w, OBJ_FILE, (const char *)face->ttf, face->ttf_len) != 0) {
        return -1;
    }
    if (begin_obj(w, OBJ_DESC) != 0) {
        return -1;
    }
    int bbox0 = pdf_scale(face, face->bbox[0]);
    int bbox1 = pdf_scale(face, face->bbox[1]);
    int bbox2 = pdf_scale(face, face->bbox[2]);
    int bbox3 = pdf_scale(face, face->bbox[3]);
    int ascent = pdf_scale(face, face->ascent);
    int descent = pdf_scale(face, face->descent);
    char name[3 * FONT_NAME_MAX + 1];
    pdf_name(face->name, name);
    if (fprintf(w->fp,
                "<< /Type /FontDescriptor /FontName /%s /Flags 32\n"
                "/FontBBox [%d %d %d %d]\n"
                "/ItalicAngle 0 /Ascent %d /Descent %d /CapHeight %d /StemV 80\n"
                "/FontFile2 %d 0 R >>\nendobj\n",
                name, bbox0, bbox1, bbox2, bbox3, ascent, descent, ascent, OBJ_FILE) < 0) {
        return -1;
    }
    if (begin_obj(w, OBJ_CID) != 0) {
        return -1;
    }
    if (fprintf(w->fp,
                "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /%s\n"
                "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>\n"
                "/FontDescriptor 5 0 R /CIDToGIDMap /Identity /DW 500\n",
                name) < 0) {
        return -1;
    }
    if (write_widths(w->fp, face) != 0) {
        return -1;
    }
    if (fputs(">>\nendobj\n", w->fp) < 0) {
        return -1;
    }
    if (write_tounicode(w, face) != 0) {
        return -1;
    }
    if (begin_obj(w, OBJ_TYPE0) != 0) {
        return -1;
    }
    if (fprintf(w->fp,
                "<< /Type /Font /Subtype /Type0 /BaseFont /%s\n"
                "/Encoding /Identity-H /DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>\n"
                "endobj\n",
                name, OBJ_CID, OBJ_CMAP) < 0) {
        return -1;
    }
    w->nobj = OBJ_CMAP;
    return 0;
}

static int utf8_next(const char *s, size_t n, size_t *i, unsigned *cp) {
    if (*i >= n) {
        return 0;
    }
    unsigned char c = (unsigned char)s[*i];
    if (c < 0x80) {
        *cp = c;
        *i += 1;
        return 1;
    }
    int need = 0;
    unsigned min = 0;
    if ((c & 0xE0) == 0xC0) {
        need = 1;
        min = 0x80;
        *cp = c & 0x1F;
        if (c < 0xC2) {
            return -1;
        }
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        min = 0x800;
        *cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        min = 0x10000;
        *cp = c & 0x07;
        if (c > 0xF4) {
            return -1;
        }
    } else {
        return -1;
    }
    if (*i + 1 + (size_t)need > n) {
        return -1;
    }
    for (int k = 0; k < need; k++) {
        unsigned char d = (unsigned char)s[*i + 1 + (size_t)k];
        if ((d & 0xC0) != 0x80) {
            return -1;
        }
        *cp = (*cp << 6) | (d & 0x3F);
    }
    if (*cp < min || *cp > 0x10FFFF) {
        return -1;
    }
    *i += 1 + (size_t)need;
    return 1;
}

static int line_too_wide(const FontFace *face, const char *s, size_t n) {
    size_t i = 0;
    long units = 0;
    long limit = (long)(PAGE_WIDTH - 2 * PAGE_MARGIN) * face->units / PAGE_FONT_PT;
    while (i < n) {
        unsigned cp = 0;
        int rc = utf8_next(s, n, &i, &cp);
        if (rc <= 0) {
            return -1;
        }
        if (cp == '\n' || cp == '\r') {
            break;
        }
        if (cp == '\t') {
            cp = ' ';
        }
        uint16_t gid = 0;
        uint16_t adv = 0;
        if (font_lookup(face, cp, &gid, &adv) != 0) {
            return -1;
        }
        units += adv;
        if (units > limit) {
            return 1;
        }
    }
    return 0;
}

/* Poppler's layout extractor drops a space whose gap equals the glyph
   advance. ActualText keeps the Unicode of the line, including spaces. */
static int append_actual_text(char *out, size_t cap, size_t *o, const char *s, size_t n) {
    const char *pre = "/Span << /ActualText <FEFF";
    size_t pre_n = strlen(pre);
    if (*o + pre_n + 16 >= cap) {
        return -1;
    }
    memcpy(out + *o, pre, pre_n);
    *o += pre_n;
    size_t i = 0;
    while (i < n) {
        unsigned cp = 0;
        int rc = utf8_next(s, n, &i, &cp);
        if (rc <= 0) {
            return -1;
        }
        if (cp == '\r') {
            cp = '\n';
        }
        unsigned hi = cp;
        unsigned lo = 0;
        int pairs = 1;
        if (cp > 0xFFFF) {
            cp -= 0x10000;
            hi = 0xD800 + (cp >> 10);
            lo = 0xDC00 + (cp & 0x3FF);
            pairs = 2;
        }
        if (*o + (size_t)pairs * 4 + 24 >= cap) {
            return -1;
        }
        *o += (size_t)sprintf(out + *o, "%04X", hi);
        if (pairs == 2) {
            *o += (size_t)sprintf(out + *o, "%04X", lo);
        }
    }
    if (*o + 12 >= cap) {
        return -1;
    }
    memcpy(out + *o, "> >> BDC\n", 10);
    *o += 10;
    return 0;
}

static int append_hex_line(char *out, size_t cap, size_t *o, const FontFace *face, const char *s, size_t n) {
    if (*o + 1 >= cap) {
        return -1;
    }
    out[(*o)++] = '<';
    size_t i = 0;
    while (i < n) {
        unsigned cp = 0;
        int rc = utf8_next(s, n, &i, &cp);
        if (rc <= 0) {
            return -1;
        }
        if (cp == '\n' || cp == '\r') {
            break;
        }
        if (cp == '\t') {
            cp = ' ';
        }
        uint16_t gid = 0;
        uint16_t adv = 0;
        if (font_lookup(face, cp, &gid, &adv) != 0) {
            return -1;
        }
        if (*o + 5 >= cap) {
            return -1;
        }
        *o += (size_t)sprintf(out + *o, "%04X", gid);
    }
    if (*o + 6 >= cap) {
        return -1;
    }
    memcpy(out + *o, "> Tj\n", 5);
    *o += 5;
    return 0;
}

void pdf_free(PdfWriter *w) {
    font_free(&w->font);
    if (w->fp != NULL && w->fp != w->raw) {
        fclose(w->fp);
        w->fp = NULL;
    }
    if (w->offsets != NULL) {
        fclose(w->offsets);
        w->offsets = NULL;
    }
    if (w->off_path != NULL) {
        unlink(w->off_path);
        free(w->off_path);
        w->off_path = NULL;
    }
    w->live_page_bytes = 0;
}

size_t pdf_live_page_bytes(const PdfWriter *w) {
    return w->live_page_bytes;
}

int pdf_page_count(const PdfWriter *w) {
    return w->npages;
}

int pdf_object_count(const PdfWriter *w) {
    return w->nobj;
}

int pdf_hit_object_limit(const PdfWriter *w) {
    return w->hit_limit;
}

struct PdfCap {
    FILE *raw;
    PdfWriter *writer;
};

static ssize_t cap_write(void *cookie, const char *buf, size_t n) {
    struct PdfCap *cap = cookie;
    PdfWriter *w = cap->writer;
    if (w->max_output > 0 && (w->written > w->max_output || (long)n > w->max_output - w->written)) {
        w->fail_kind = 4;
        w->hit_limit = 1;
        errno = EFBIG;
        return -1;
    }
    if (fwrite(buf, 1, n, cap->raw) != n) {
        w->fail_kind = 2;
        return -1;
    }
    w->written += (long)n;
    return (ssize_t)n;
}

static int cap_close(void *cookie) {
    free(cookie);
    return 0;
}

int pdf_begin_cfg(PdfWriter *w, FILE *fp, const PdfConfig *cfg) {
    memset(w, 0, sizeof *w);
    w->raw = fp;
    w->max_pages = cfg != NULL ? cfg->max_pages : 0;
    w->max_output = cfg != NULL ? cfg->max_output : 0;
    w->max_spool = cfg != NULL && cfg->max_spool > 0 ? cfg->max_spool : (64L * 1024L * 1024L);
    w->debug_fail_page = cfg != NULL ? cfg->debug_fail_page : 0;
    w->cancelled = cfg != NULL ? cfg->cancelled : NULL;
    w->cancel_user = cfg != NULL ? cfg->cancel_user : NULL;
    const char *tdir = cfg != NULL && cfg->temp_dir != NULL && cfg->temp_dir[0] != '\0' ? cfg->temp_dir : "/tmp";
    size_t path_n = strlen(tdir) + 24;
    w->off_path = malloc(path_n);
    if (w->off_path == NULL) {
        w->fail_kind = 1;
        return -1;
    }
    int nw = snprintf(w->off_path, path_n, "%s/ki-xml-pdf-off-XXXXXX", tdir);
    if (nw < 0 || (size_t)nw >= path_n) {
        free(w->off_path);
        w->off_path = NULL;
        w->fail_kind = 2;
        w->fail_errno = ENAMETOOLONG;
        return -1;
    }
    int fd = mkstemp(w->off_path);
    if (fd < 0) {
        w->fail_errno = errno;
        free(w->off_path);
        w->off_path = NULL;
        w->fail_kind = 2;
        return -1;
    }
    w->offsets = fdopen(fd, "w+b");
    if (w->offsets == NULL) {
        w->fail_errno = errno;
        close(fd);
        unlink(w->off_path);
        free(w->off_path);
        w->off_path = NULL;
        w->fail_kind = 2;
        return -1;
    }
    int loaded;
    if (cfg != NULL && cfg->font_path != NULL && cfg->font_path[0] != '\0') {
        loaded = font_load(&w->font, cfg->font_path);
    } else {
        loaded = font_open_default(&w->font);
    }
    if (loaded != 0) {
        w->fail_kind = 3;
        pdf_free(w);
        return -1;
    }
    struct PdfCap *cap = malloc(sizeof *cap);
    if (cap == NULL) {
        w->fail_kind = 1;
        pdf_free(w);
        return -1;
    }
    cap->raw = fp;
    cap->writer = w;
    cookie_io_functions_t fn;
    memset(&fn, 0, sizeof fn);
    fn.write = cap_write;
    fn.close = cap_close;
    w->fp = fopencookie(cap, "w", fn);
    if (w->fp == NULL) {
        free(cap);
        w->fail_kind = 2;
        pdf_free(w);
        return -1;
    }
    if (fputs("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n", w->fp) < 0) {
        pdf_free(w);
        return -1;
    }
    if (write_font_objects(w) != 0) {
        if (w->fail_kind == 0) {
            w->fail_kind = 2;
        }
        pdf_free(w);
        return -1;
    }
    return 0;
}

int pdf_begin(PdfWriter *w, FILE *fp) {
    return pdf_begin_cfg(w, fp, NULL);
}

void pdf_debug_fail_at_page(int n) {
    (void)n;
}

int pdf_add_page(PdfWriter *w, const char *text, size_t len) {
    w->debug_pages++;
    if (w->debug_fail_page > 0 && w->debug_pages >= w->debug_fail_page) {
        w->fail_kind = 2;
        return -1;
    }
    if (w->max_pages > 0 && w->npages >= w->max_pages) {
        w->hit_limit = 1;
        w->fail_kind = 1;
        return -1;
    }
    /* Worst case per line: four hex digits per byte in both ActualText and
       the glyph string, plus operators. */
    size_t cap = 256 + (size_t)PAGE_MAX_LINES * (8 * PDF_LINE_BYTES + 64);
    char *stream = malloc(cap);
    if (stream == NULL) {
        return -1;
    }
    int baseline = font_first_baseline(&w->font, 792, PAGE_MARGIN, PAGE_FONT_PT);
    int max_lines = font_max_lines(&w->font, 792, PAGE_MARGIN, PAGE_FONT_PT, PAGE_LEADING);
    char prefix[64];
    int npre = sprintf(prefix, "BT\n/F1 %d Tf\n%d TL\n%d %d Td\n", PAGE_FONT_PT, PAGE_LEADING, PAGE_MARGIN,
                       baseline);
    if (npre < 0 || (size_t)npre >= cap) {
        free(stream);
        return -1;
    }
    size_t o = (size_t)npre;
    memcpy(stream, prefix, o);
    size_t i = 0;
    int lines = 0;
    int any = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t n = i - start;
        if (i < len && text[i] == '\n') {
            i++;
        }
        int wide = line_too_wide(&w->font, text + start, n);
        if (wide != 0) {
            free(stream);
            return -1;
        }
        if (lines >= max_lines || lines >= PAGE_MAX_LINES) {
            free(stream);
            return -1;
        }
        if (any) {
            if (o + 3 >= cap) {
                free(stream);
                return -1;
            }
            memcpy(stream + o, "T*\n", 3);
            o += 3;
        }
        any = 1;
        if (append_actual_text(stream, cap, &o, text + start, n) != 0 ||
            append_hex_line(stream, cap, &o, &w->font, text + start, n) != 0) {
            free(stream);
            return -1;
        }
        if (o + 4 >= cap) {
            free(stream);
            return -1;
        }
        memcpy(stream + o, "EMC\n", 4);
        o += 4;
        lines++;
    }
    if (!any) {
        if (o + 6 >= cap) {
            free(stream);
            return -1;
        }
        memcpy(stream + o, "<> Tj\n", 6);
        o += 6;
    }
    if (o + 3 >= cap) {
        free(stream);
        return -1;
    }
    memcpy(stream + o, "ET\n", 3);
    o += 3;
    w->live_page_bytes = o;

    int cid = OBJ_FIRST + 2 * w->npages;
    int pid = cid + 1;
    int rc = -1;
    if (write_stream_obj(w, cid, stream, o) != 0) {
        goto done;
    }
    if (begin_obj(w, pid) != 0) {
        goto done;
    }
    if (fprintf(w->fp,
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]\n"
                "/Contents %d 0 R\n"
                "/Resources << /Font << /F1 %d 0 R >> >> >>\nendobj\n",
                cid, OBJ_TYPE0) < 0) {
        goto done;
    }
    w->npages++;
    w->nobj = pid;
    rc = 0;
done:
    free(stream);
    w->live_page_bytes = 0;
    return rc;
}

static int stop_pdf(PdfWriter *w) {
    if (w->cancelled != NULL && w->cancelled(w->cancel_user)) {
        w->fail_kind = 6;
        return 1;
    }
    return 0;
}

int pdf_end(PdfWriter *w) {
    if (stop_pdf(w) || begin_obj(w, OBJ_PAGES) != 0) {
        return -1;
    }
    if (fprintf(w->fp, "<< /Type /Pages /Count %d /Kids [", w->npages) < 0) {
        return -1;
    }
    for (int i = 0; i < w->npages; i++) {
        if ((i % 64) == 0 && stop_pdf(w)) {
            return -1;
        }
        int pid = OBJ_FIRST + 2 * i + 1;
        if (fprintf(w->fp, "%d 0 R ", pid) < 0) {
            return -1;
        }
        if ((i + 1) % 8 == 0 && fputc('\n', w->fp) == EOF) {
            return -1;
        }
    }
    if (fputs("] >>\nendobj\n", w->fp) < 0) {
        return -1;
    }
    if (begin_obj(w, OBJ_CATALOG) != 0) {
        return -1;
    }
    if (fputs("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", w->fp) < 0) {
        return -1;
    }
    if (fflush(w->offsets) != 0) {
        return -1;
    }
    struct stat st;
    if (fstat(fileno(w->offsets), &st) == 0) {
        w->spool_bytes = (long)st.st_size;
    }
    if (fflush(w->fp) != 0) {
        return -1;
    }
    long xref_off = w->written;
    if (fprintf(w->fp, "xref\n0 %d\n", w->nobj + 1) < 0) {
        return -1;
    }
    if (fputs("0000000000 65535 f \n", w->fp) < 0) {
        return -1;
    }
    for (int i = 1; i <= w->nobj; i++) {
        if ((i % 64) == 1 && stop_pdf(w)) {
            return -1;
        }
        long off = 0;
        if (fseek(w->offsets, (long)i * 8, SEEK_SET) != 0) {
            return -1;
        }
        if (fread(&off, sizeof off, 1, w->offsets) != 1 || off <= 0) {
            return -1;
        }
        if (fprintf(w->fp, "%010ld 00000 n \n", off) < 0) {
            return -1;
        }
    }
    if (fprintf(w->fp, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", w->nobj + 1,
                xref_off) < 0) {
        return -1;
    }
    if (fflush(w->fp) != 0) {
        return -1;
    }
    return 0;
}
