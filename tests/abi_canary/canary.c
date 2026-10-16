/* SPDX-License-Identifier: MIT */
/* ABI canary for the 0.2 public API. It is compiled once against an
   installed 0.2 header and linked dynamically (soname libki_xml_pdf.so.0).
   The resulting binary is kept and later run, without recompiling, against a
   newer libki_xml_pdf.so.0 through LD_LIBRARY_PATH. It prints the struct
   layout it was compiled with and exercises the three conversion entries,
   a custom mapper, report counters and destination preservation.

   usage: abi_canary_c OUTDIR [FONT]
   Exit 0 only when every check passed. */
#include "ki_xml_pdf.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT(cond, what)                                   \
    do {                                                     \
        if (!(cond)) {                                       \
            fprintf(stderr, "canary FAIL %s\n", what);       \
            failures++;                                      \
        }                                                    \
    } while (0)

#define OFF(type, field) printf(" %s=%zu", #field, offsetof(type, field))

static void print_layout(void) {
    printf("layout KiOptions size=%zu", sizeof(KiOptions));
    OFF(KiOptions, size);
    OFF(KiOptions, font_path);
    OFF(KiOptions, temp_dir);
    OFF(KiOptions, max_pages);
    OFF(KiOptions, max_output_bytes);
    OFF(KiOptions, max_spool_bytes);
    OFF(KiOptions, profile);
    OFF(KiOptions, record_offset);
    OFF(KiOptions, record_limit);
    OFF(KiOptions, cancelled);
    OFF(KiOptions, cancel_user);
    OFF(KiOptions, map);
    OFF(KiOptions, map_user);
    OFF(KiOptions, log);
    OFF(KiOptions, log_user);
    OFF(KiOptions, read_bytes);
    OFF(KiOptions, write_bytes);
    OFF(KiOptions, io_user);
    printf("\n");
    printf("layout KiResult size=%zu", sizeof(KiResult));
    OFF(KiResult, status);
    OFF(KiResult, cause);
    OFF(KiResult, message);
    OFF(KiResult, pages);
    OFF(KiResult, objects);
    OFF(KiResult, offset_bytes);
    OFF(KiResult, output_bytes);
    OFF(KiResult, spool_bytes);
    OFF(KiResult, report_total);
    OFF(KiResult, report_selected);
    OFF(KiResult, report_skipped);
    printf("\n");
    printf("layout KiEvent size=%zu", sizeof(KiEvent));
    OFF(KiEvent, kind);
    OFF(KiEvent, name);
    OFF(KiEvent, nattr);
    OFF(KiEvent, attr_name);
    OFF(KiEvent, attr_val);
    OFF(KiEvent, text);
    OFF(KiEvent, text_len);
    OFF(KiEvent, from_cdata);
    OFF(KiEvent, byte_offset);
    printf("\n");
    printf("header version=%s profile=%s causes=%d,%d,%d,%d,%d,%d,%d,%d\n", KI_XML_PDF_VERSION,
           KI_XML_PDF_PROFILE, KI_CAUSE_NONE, KI_CAUSE_XML, KI_CAUSE_USAGE, KI_CAUSE_IO,
           KI_CAUSE_RESOURCE, KI_CAUSE_FONT, KI_CAUSE_CANCEL, KI_CAUSE_PROFILE);
}

typedef struct {
    const char *data;
    size_t len;
    size_t pos;
} MemIn;

/* One byte per call: a short positive read must not be treated as EOF. */
static int mem_read(void *user, char *dst, int max) {
    MemIn *in = user;
    if (max <= 0) {
        return -1;
    }
    if (in->pos >= in->len) {
        return 0;
    }
    dst[0] = in->data[in->pos++];
    return 1;
}

typedef struct {
    MemIn in;
    FILE *out;
} CbIo;

static int cb_read(void *user, char *dst, int max) {
    return mem_read(&((CbIo *)user)->in, dst, max);
}

static int cb_write(void *user, const char *src, int n) {
    CbIo *io = user;
    return fwrite(src, 1, (size_t)n, io->out) == (size_t)n ? n : -1;
}

static int write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, fp) == n;
    return (fclose(fp) == 0 && ok) ? 0 : -1;
}

static int file_equals(const char *path, const char *text) {
    char buf[64];
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    return n == strlen(text) && memcmp(buf, text, n) == 0;
}

static int mapper_calls;

static int canary_map(void *user, KiDoc *doc, const KiEvent *ev) {
    (void)user;
    mapper_calls++;
    if (ev->kind == KI_EV_START && strcmp(ev->name, "bad") == 0) {
        return ki_doc_fail(doc, "canary: bad element");
    }
    if (ev->kind == KI_EV_TEXT && ev->text_len > 0) {
        static const char prefix[] = "MAPPED:";
        if (ki_doc_text(doc, prefix, sizeof prefix - 1) != 0 ||
            ki_doc_text(doc, ev->text, ev->text_len) != 0 || ki_doc_break(doc) != 0) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s OUTDIR [FONT]\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1];
    const char *font = argc > 2 ? argv[2] : NULL;
    char in_path[4096], pdf_path[4096];
    print_layout();

    KiOptions opt;
    KiResult res;

    /* ki_convert_file, records-v1 */
    snprintf(in_path, sizeof in_path, "%s/c-file.xml", dir);
    snprintf(pdf_path, sizeof pdf_path, "%s/c-file.pdf", dir);
    EXPECT(write_file(in_path, "<doc><item id=\"7\">ABI_C_FILE_MARKER</item></doc>") == 0,
           "write input");
    ki_options_init(&opt);
    EXPECT(opt.size == sizeof(KiOptions), "ki_options_init size");
    opt.font_path = font;
    int rc = ki_convert_file(in_path, pdf_path, &opt, &res);
    EXPECT(rc == 0 && res.status == 0 && res.cause == KI_CAUSE_NONE && res.pages == 1,
           "ki_convert_file records-v1");
    printf("entry ki_convert_file rc=%d pages=%d objects=%d output_bytes=%ld\n", rc, res.pages,
           res.objects, res.output_bytes);

    /* ki_convert, report-v1 with one-byte reads and selection counters */
    static const char report[] =
        "<report version=\"1\" title=\"ABI_C_REPORT\">"
        "<record id=\"a\"><field name=\"v\">ABI_C_REC_A</field></record>"
        "<record id=\"b\"><field name=\"v\">ABI_C_REC_B</field></record>"
        "<record id=\"c\"><field name=\"v\">ABI_C_REC_C</field></record>"
        "</report>";
    snprintf(pdf_path, sizeof pdf_path, "%s/c-callback.pdf", dir);
    CbIo io;
    io.in.data = report;
    io.in.len = sizeof report - 1;
    io.in.pos = 0;
    io.out = fopen(pdf_path, "wb");
    EXPECT(io.out != NULL, "open callback output");
    ki_options_init(&opt);
    opt.font_path = font;
    opt.profile = KI_PROFILE_REPORT;
    opt.record_limit = 2;
    opt.read_bytes = cb_read;
    opt.write_bytes = cb_write;
    opt.io_user = &io;
    rc = io.out != NULL ? ki_convert(&opt, &res) : -1;
    if (io.out != NULL) {
        EXPECT(fclose(io.out) == 0, "close callback output");
    }
    EXPECT(rc == 0 && res.report_total == 3 && res.report_selected == 2 &&
               res.report_skipped == 1,
           "ki_convert report counters");
    printf("entry ki_convert rc=%d total=%llu selected=%llu skipped=%llu\n", rc,
           res.report_total, res.report_selected, res.report_skipped);

    /* ki_convert_to_file with a custom mapper */
    static const char mapped[] = "<doc><a>ABI_C_MAPPED_ONE</a><b>ABI_C_MAPPED_TWO</b></doc>";
    snprintf(pdf_path, sizeof pdf_path, "%s/c-mapper.pdf", dir);
    MemIn min = {mapped, sizeof mapped - 1, 0};
    ki_options_init(&opt);
    opt.font_path = font;
    opt.map = canary_map;
    opt.read_bytes = mem_read;
    opt.io_user = &min;
    rc = ki_convert_to_file(pdf_path, &opt, &res);
    EXPECT(rc == 0 && mapper_calls > 0, "ki_convert_to_file custom mapper");
    printf("entry ki_convert_to_file rc=%d mapper_calls=%d\n", rc, mapper_calls);

    /* A failed conversion keeps the previous destination. */
    snprintf(in_path, sizeof in_path, "%s/c-bad.xml", dir);
    snprintf(pdf_path, sizeof pdf_path, "%s/c-kept.pdf", dir);
    EXPECT(write_file(in_path, "<report version=\"1\" title=\"t\"><nope/></report>") == 0,
           "write bad input");
    EXPECT(write_file(pdf_path, "PREVIOUS") == 0, "write previous destination");
    ki_options_init(&opt);
    opt.font_path = font;
    opt.profile = KI_PROFILE_REPORT;
    rc = ki_convert_file(in_path, pdf_path, &opt, &res);
    EXPECT(rc == 1 && res.cause == KI_CAUSE_PROFILE && file_equals(pdf_path, "PREVIOUS"),
           "profile failure keeps destination");
    printf("entry ki_convert_file_fail rc=%d cause=%d kept=%d\n", rc, res.cause,
           file_equals(pdf_path, "PREVIOUS"));

    if (failures != 0) {
        printf("canary c failures=%d\n", failures);
        return 1;
    }
    printf("canary c ok\n");
    return 0;
}
