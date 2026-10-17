#define _GNU_SOURCE

#include "platform.h"
#include "convert.h"

#include "geometry.h"
#include "info.h"
#include "ki_xml_pdf.h"
#include "layout.h"
#include "pdf_write.h"
#include "report.h"
#include "xml_parse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_at_page;
static int fail_before_publish;
static long fail_after_bytes;

void xml2pdf_debug_fail_at_page(int n) {
    fail_at_page = n;
}

void xml2pdf_debug_fail_before_publish(int on) {
    fail_before_publish = on;
}

void xml2pdf_debug_fail_after_bytes(long n) {
    fail_after_bytes = n;
}

#define LINE_FULL_MSG "layout: line over 1023 bytes"
#define TOO_WIDE_MSG "layout: glyph cluster wider than the line"

static int text_is_space(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
            return 0;
        }
    }
    return 1;
}

static int emit_str(Layout *lay, const char *s) {
    if (layout_text(lay, s, strlen(s)) != 0) {
        return -1;
    }
    return layout_break(lay);
}

typedef struct {
    int depth;
    unsigned char preserve[XML_MAX_DEPTH];
} MapState;

/* Indentation that contains a newline is dropped only when the current line is
   empty and xml:space is not preserve. CDATA whitespace and spaces inside a
   line are kept, so a split "A" + " " + "B" stays "A B". */
static int map_event(Layout *lay, MapState *st, const XmlEvent *ev) {
    if (ev->kind == XML_EV_START) {
        if (st->depth >= XML_MAX_DEPTH) {
            return -1;
        }
        int keep = st->depth > 0 ? st->preserve[st->depth - 1] : 0;
        for (int i = 0; i < ev->nattr; i++) {
            if (strcmp(ev->attr_name[i], "xml:space") == 0) {
                keep = strcmp(ev->attr_val[i], "preserve") == 0;
            }
        }
        st->preserve[st->depth] = (unsigned char)keep;
        st->depth++;
        if (emit_str(lay, ev->name) != 0) {
            return -1;
        }
        for (int i = 0; i < ev->nattr; i++) {
            if (layout_text(lay, ev->attr_name[i], strlen(ev->attr_name[i])) != 0 ||
                layout_text(lay, ": ", 2) != 0 ||
                layout_text(lay, ev->attr_val[i], strlen(ev->attr_val[i])) != 0 ||
                layout_break(lay) != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (ev->kind == XML_EV_END) {
        if (st->depth > 0) {
            st->depth--;
        }
        return layout_break(lay);
    }
    if (ev->kind == XML_EV_TEXT) {
        if (ev->text_len == 0) {
            return 0;
        }
        int only_ws = text_is_space(ev->text, ev->text_len);
        int has_break = 0;
        for (size_t i = 0; i < ev->text_len; i++) {
            if (ev->text[i] == '\n' || ev->text[i] == '\r') {
                has_break = 1;
                break;
            }
        }
        int preserve = ev->from_cdata || (st->depth > 0 && st->preserve[st->depth - 1]);
        if (only_ws && has_break && layout_line_empty(lay) && !preserve) {
            return 0;
        }
        return layout_text(lay, ev->text, ev->text_len);
    }
    return 0;
}

typedef struct {
    FILE *fp;
    const KiOptions *opt;
    long bytes;
    int io_failed;
    int cancelled;
} ReadState;

static int conv_read(void *user, char *dst, int max) {
    ReadState *st = user;
    if (st->opt != NULL && st->opt->cancelled != NULL && st->opt->cancelled(st->opt->cancel_user)) {
        st->cancelled = 1;
        return -1;
    }
    if (fail_after_bytes > 0 && st->bytes >= fail_after_bytes) {
        st->io_failed = 1;
        return -1;
    }
    if (max <= 0) {
        return 0;
    }
    errno = 0;
    int n = 0;
    if (st->fp != NULL) {
        size_t got = fread(dst, 1, (size_t)max, st->fp);
        if (st->opt != NULL && st->opt->cancelled != NULL && st->opt->cancelled(st->opt->cancel_user)) {
            st->cancelled = 1;
            return -1;
        }
        if (got == 0) {
            if (ferror(st->fp) || errno == EINTR) {
                st->io_failed = 1;
                return -1;
            }
            return 0;
        }
        n = (int)got;
    } else if (st->opt != NULL && st->opt->read_bytes != NULL) {
        n = st->opt->read_bytes(st->opt->io_user, dst, max);
        if (st->opt->cancelled != NULL && st->opt->cancelled(st->opt->cancel_user)) {
            st->cancelled = 1;
            return -1;
        }
        if (n < 0 || n > max) {
            st->io_failed = 1;
            return -1;
        }
        if (n == 0) {
            return 0;
        }
    } else {
        st->io_failed = 1;
        return -1;
    }
    st->bytes += n;
    return n;
}

static char *join_dir(const char *dir, const char *base) {
    size_t ld = strlen(dir);
    size_t lb = strlen(base);
    int need_slash = ld > 0 && dir[ld - 1] != '/';
    char *out = malloc(ld + lb + 2);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, dir, ld);
    if (need_slash) {
        out[ld++] = '/';
    }
    memcpy(out + ld, base, lb + 1);
    return out;
}

/* Path the output file would occupy. Existing links are followed so a
   symlink or hard link to the input compares equal to it. */
static char *output_identity(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return realpath(path, NULL);
    }
    char *copy = strdup(path);
    if (copy == NULL) {
        return NULL;
    }
    char *slash = strrchr(copy, '/');
    char *dirpart = NULL;
    const char *base = copy;
    if (slash == NULL) {
        dirpart = strdup(".");
    } else if (slash == copy) {
        dirpart = strdup("/");
        base = slash + 1;
    } else {
        *slash = '\0';
        dirpart = strdup(copy);
        base = slash + 1;
    }
    char *dirreal = dirpart != NULL ? realpath(dirpart, NULL) : NULL;
    char *full = NULL;
    if (dirreal != NULL && base[0] != '\0') {
        full = join_dir(dirreal, base);
    }
    free(dirpart);
    free(dirreal);
    free(copy);
    return full;
}

static int same_file(const char *in_path, const char *out_path) {
    struct stat sa, sb;
    if (stat(in_path, &sa) == 0 && stat(out_path, &sb) == 0 && sa.st_dev == sb.st_dev &&
        sa.st_ino == sb.st_ino) {
        return 1;
    }
    char *a = realpath(in_path, NULL);
    char *b = output_identity(out_path);
    int same = a != NULL && b != NULL && strcmp(a, b) == 0;
    free(a);
    free(b);
    return same;
}

static char *make_temp(const char *path, int *fd_out) {
    size_t n = strlen(path);
    char *tmpl = malloc(n + 8);
    if (tmpl == NULL) {
        return NULL;
    }
    memcpy(tmpl, path, n);
    memcpy(tmpl + n, ".XXXXXX", 8);
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        free(tmpl);
        return NULL;
    }
    *fd_out = fd;
    return tmpl;
}

struct KiDoc {
    Layout *lay;
    char error[160];
    int cause;
    size_t line;
    size_t column;
    size_t byte;
};

void ki_options_init(KiOptions *opt) {
    if (opt == NULL) {
        return;
    }
    memset(opt, 0, sizeof *opt);
    opt->size = sizeof *opt;
}

int ki_doc_fail(KiDoc *doc, const char *rule) {
    if (doc != NULL) {
        doc->cause = KI_CAUSE_PROFILE;
        xml_format_pos(doc->error, sizeof doc->error, rule != NULL ? rule : "mapping failed", doc->line, doc->column,
                       doc->byte);
    }
    return -1;
}

int ki_doc_text(KiDoc *doc, const char *s, size_t n) {
    if (doc == NULL || doc->lay == NULL) {
        return -1;
    }
    int rc = layout_text(doc->lay, s, n);
    if (rc != 0 && doc->lay->bad_cp != 0) {
        doc->cause = KI_CAUSE_FONT;
        snprintf(doc->error, sizeof doc->error, "font: U+%04X", doc->lay->bad_cp);
    } else if (rc != 0 && (doc->lay->line_full || doc->lay->too_wide)) {
        doc->cause = KI_CAUSE_RESOURCE;
        snprintf(doc->error, sizeof doc->error, "%s", doc->lay->line_full ? LINE_FULL_MSG : TOO_WIDE_MSG);
    }
    return rc;
}

int ki_doc_break(KiDoc *doc) {
    if (doc == NULL || doc->lay == NULL) {
        return -1;
    }
    return layout_break(doc->lay);
}

static void say(const KiOptions *opt, const char *msg) {
    if (opt != NULL && opt->log != NULL && msg != NULL) {
        opt->log(opt->log_user, msg);
    }
}

static void set_result(KiResult *result, int status, int cause, const char *msg) {
    if (result == NULL) {
        return;
    }
    result->status = status;
    result->cause = cause;
    snprintf(result->message, sizeof result->message, "%s", msg != NULL ? msg : "");
}

static int fail_from_pdf(PdfWriter *pdf, int *cause, const char **msg) {
    if (pdf->fail_kind == PDF_FAIL_CANCEL) {
        *cause = KI_CAUSE_CANCEL;
        *msg = "cancelled";
        return XML2PDF_ERR_RESOURCE;
    }
    if (pdf->fail_kind == PDF_FAIL_FONT) {
        *cause = KI_CAUSE_FONT;
        *msg = "font: cannot load";
        return XML2PDF_ERR_XML;
    }
    if (pdf->fail_kind == PDF_FAIL_GEOMETRY) {
        *cause = KI_CAUSE_USAGE;
        *msg = "page geometry: no line fits this font inside the margins";
        return XML2PDF_ERR_USAGE;
    }
    if (pdf->fail_kind == PDF_FAIL_XREF) {
        *cause = KI_CAUSE_RESOURCE;
        *msg = "resource limit: PDF exceeds classic xref offsets";
        return XML2PDF_ERR_RESOURCE;
    }
    if (pdf->fail_kind == PDF_FAIL_LIMIT || pdf->fail_kind == PDF_FAIL_QUOTA || pdf->fail_kind == PDF_FAIL_SPOOL ||
        pdf->hit_limit) {
        *cause = KI_CAUSE_RESOURCE;
        *msg = "resource limit";
        return XML2PDF_ERR_RESOURCE;
    }
    if (pdf->fail_kind == PDF_FAIL_FLATE) {
        *cause = KI_CAUSE_IO;
        *msg = "pdf compression failed";
        return XML2PDF_ERR_IO;
    }
    if (pdf->fail_errno == ENAMETOOLONG) {
        *cause = KI_CAUSE_IO;
        *msg = "temp path too long";
        return XML2PDF_ERR_IO;
    }
    *cause = KI_CAUSE_IO;
    *msg = pdf->fail_errno != 0 ? "cannot create offset file" : "pdf write failed";
    return XML2PDF_ERR_IO;
}

/* Validated copies of a KiConfig, owned by the entry point's frame. */
typedef struct Setup {
    Geometry geom;
    PdfInfo info;
} Setup;

static int convert_streams(FILE *in, FILE *out, char *tmp_path, const char *pdf_path, const KiOptions *opt,
                           const Setup *setup, KiResult *result);

static int reject_options(const KiOptions *opt, KiResult *result) {
    if (opt == NULL) {
        return 0;
    }
    if (opt->size != sizeof(KiOptions)) {
        set_result(result, XML2PDF_ERR_USAGE, KI_CAUSE_USAGE, "options size mismatch");
        return -1;
    }
    if (opt->max_pages < 0 || opt->max_output_bytes < 0 || opt->max_spool_bytes < 0 || opt->record_offset < 0 ||
        opt->record_limit < 0 || (opt->profile != KI_PROFILE_RECORDS && opt->profile != KI_PROFILE_REPORT)) {
        set_result(result, XML2PDF_ERR_USAGE, KI_CAUSE_USAGE, "invalid option");
        return -1;
    }
    return 0;
}

/* Fills setup from cfg (defaults when NULL). Static checks only: whether a
   line fits is known once the font is loaded. */
static int reject_config(const KiConfig *cfg, const KiOptions *opt, Setup *setup, KiResult *result) {
    geom_default(&setup->geom);
    info_clear(&setup->info);
    if (cfg == NULL) {
        return 0;
    }
    if (cfg->size != sizeof(KiConfig) && cfg->size != KI_CONFIG_SIZE_GEOMETRY) {
        set_result(result, XML2PDF_ERR_USAGE, KI_CAUSE_USAGE, "config size mismatch");
        say(opt, "config size mismatch");
        return -1;
    }
    const double margin[4] = {cfg->margin_top, cfg->margin_right, cfg->margin_bottom, cfg->margin_left};
    const char *why = NULL;
    int bad = geom_from_points(&setup->geom, cfg->page_size, cfg->orientation, cfg->page_width, cfg->page_height,
                               margin, cfg->font_size, &why) != 0;
    if (!bad && cfg->size == sizeof(KiConfig)) {
        bad = info_set(&setup->info, cfg->title, cfg->producer, cfg->creation_date, &why) != 0;
    }
    if (bad) {
        set_result(result, XML2PDF_ERR_USAGE, KI_CAUSE_USAGE, why);
        say(opt, why);
        return -1;
    }
    return 0;
}

int ki_config_init(KiConfig *cfg, size_t size) {
    if (cfg == NULL || (size != sizeof(KiConfig) && size != KI_CONFIG_SIZE_GEOMETRY)) {
        return XML2PDF_ERR_USAGE;
    }
    KiConfig def;
    memset(&def, 0, sizeof def);
    def.size = size;
    def.page_size = KI_PAGE_LETTER;
    def.orientation = KI_ORIENT_PORTRAIT;
    def.margin_top = def.margin_right = def.margin_bottom = def.margin_left = 40.0;
    def.font_size = 10.0;
    memcpy(cfg, &def, size);
    return 0;
}

static int convert_file_stream(FILE *in, const char *pdf_path, const KiOptions *opt, const Setup *setup,
                               KiResult *result) {
    int rc = XML2PDF_ERR_IO;
    int cause = KI_CAUSE_IO;
    FILE *out = NULL;
    char *tmp_path = NULL;
    char msg[160];
    int tmp_fd = -1;
    errno = 0;
    tmp_path = make_temp(pdf_path, &tmp_fd);
    if (tmp_path == NULL) {
        if (errno == ENOSPC || errno == EDQUOT) {
            rc = XML2PDF_ERR_RESOURCE;
            cause = KI_CAUSE_RESOURCE;
        } else {
            rc = XML2PDF_ERR_IO;
            cause = KI_CAUSE_IO;
        }
        snprintf(msg, sizeof msg, "cannot create temporary output for %s", pdf_path);
        say(opt, msg);
        goto open_fail;
    }
    out = fdopen(tmp_fd, "wb");
    if (out == NULL) {
        close(tmp_fd);
        rc = XML2PDF_ERR_IO;
        cause = KI_CAUSE_IO;
        snprintf(msg, sizeof msg, "cannot open temporary output %s", tmp_path);
        say(opt, msg);
        goto open_fail;
    }
    setvbuf(out, NULL, _IOFBF, 4096);
    return convert_streams(in, out, tmp_path, pdf_path, opt, setup, result);

open_fail:
    set_result(result, rc, cause, msg);
    if (out != NULL) {
        fclose(out);
    }
    if (in != NULL) {
        fclose(in);
    }
    if (tmp_path != NULL) {
        unlink(tmp_path);
    }
    free(tmp_path);
    return rc;
}

int ki_convert_file(const char *xml_path, const char *pdf_path, const KiOptions *opt, KiResult *result) {
    return ki_convert_file_cfg(xml_path, pdf_path, opt, NULL, result);
}

int ki_convert_file_cfg(const char *xml_path, const char *pdf_path, const KiOptions *opt, const KiConfig *cfg,
                        KiResult *result) {
    if (result != NULL) {
        memset(result, 0, sizeof *result);
    }
    Setup setup;
    if (reject_options(opt, result) != 0 || reject_config(cfg, opt, &setup, result) != 0) {
        return XML2PDF_ERR_USAGE;
    }
    if (xml_path == NULL || pdf_path == NULL) {
        set_result(result, XML2PDF_ERR_USAGE, KI_CAUSE_USAGE, "missing path");
        say(opt, "missing path");
        return XML2PDF_ERR_USAGE;
    }
    if (same_file(xml_path, pdf_path)) {
        set_result(result, XML2PDF_ERR_IO, KI_CAUSE_IO, "input and output are the same file");
        say(opt, "input and output are the same file");
        return XML2PDF_ERR_IO;
    }
    FILE *in = fopen(xml_path, "rb");
    char msg[160];
    if (in == NULL) {
        snprintf(msg, sizeof msg, "cannot open input %s", xml_path);
        set_result(result, XML2PDF_ERR_IO, KI_CAUSE_IO, msg);
        say(opt, msg);
        return XML2PDF_ERR_IO;
    }
    struct stat input_st;
    if (fstat(fileno(in), &input_st) == 0 && S_ISDIR(input_st.st_mode)) {
        snprintf(msg, sizeof msg, "cannot read input %s", xml_path);
        set_result(result, XML2PDF_ERR_IO, KI_CAUSE_IO, msg);
        say(opt, msg);
        fclose(in);
        return XML2PDF_ERR_IO;
    }
    return convert_file_stream(in, pdf_path, opt, &setup, result);
}

int ki_convert_to_file(const char *pdf_path, const KiOptions *opt, KiResult *result) {
    return ki_convert_to_file_cfg(pdf_path, opt, NULL, result);
}

int ki_convert_to_file_cfg(const char *pdf_path, const KiOptions *opt, const KiConfig *cfg, KiResult *result) {
    if (result != NULL) {
        memset(result, 0, sizeof *result);
    }
    Setup setup;
    if (reject_options(opt, result) != 0 || reject_config(cfg, opt, &setup, result) != 0) {
        return XML2PDF_ERR_USAGE;
    }
    if (pdf_path == NULL || opt == NULL || opt->read_bytes == NULL) {
        const char *msg = pdf_path == NULL ? "missing path" : "read callback is required";
        set_result(result, XML2PDF_ERR_USAGE, KI_CAUSE_USAGE, msg);
        return XML2PDF_ERR_USAGE;
    }
    return convert_file_stream(NULL, pdf_path, opt, &setup, result);
}

static int convert_streams(FILE *in, FILE *out, char *tmp_path, const char *pdf_path, const KiOptions *opt,
                           const Setup *setup, KiResult *result) {
    int rc = XML2PDF_ERR_XML;
    int cause = KI_CAUSE_XML;
    int published = 0;
    int pages = 0;
    int objects = 0;
    long written = 0;
    long spool = 0;
    int use_report = 0;
    Report report_state;
    Layout lay;
    PdfWriter pdf;
    int pdf_ready = 0;
    char msg[160];
    char glyph_msg[32];
    memset(&lay, 0, sizeof lay);
    memset(&pdf, 0, sizeof pdf);
    if (result != NULL) {
        memset(result, 0, sizeof *result);
    }
    msg[0] = '\0';

    ReadState reader;
    memset(&reader, 0, sizeof reader);
    reader.fp = in;
    reader.opt = opt;
    XmlParser parser;
    if (xml_parser_init(&parser, conv_read, &reader) != 0) {
        snprintf(msg, sizeof msg, "parser init failed");
        say(opt, msg);
        goto done;
    }
    PdfConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    if (opt != NULL) {
        cfg.font_path = opt->font_path;
        cfg.temp_dir = opt->temp_dir;
        cfg.max_pages = opt->max_pages;
        cfg.max_output = opt->max_output_bytes;
        cfg.max_spool = opt->max_spool_bytes;
        cfg.cancelled = opt->cancelled;
        cfg.cancel_user = opt->cancel_user;
    }
    cfg.debug_fail_page = fail_at_page;
    cfg.geometry = &setup->geom;
    cfg.info = &setup->info;
    if (pdf_begin_cfg(&pdf, out, &cfg) != 0) {
        pdf_ready = 1;
        const char *why = NULL;
        rc = fail_from_pdf(&pdf, &cause, &why);
        snprintf(msg, sizeof msg, "%s", why);
        say(opt, msg);
        goto done;
    }
    pdf_ready = 1;
    if (layout_init(&lay, &pdf) != 0) {
        snprintf(msg, sizeof msg, "layout init failed");
        say(opt, msg);
        goto done;
    }
    KiDoc doc;
    memset(&doc, 0, sizeof doc);
    doc.lay = &lay;
    MapState map;
    memset(&map, 0, sizeof map);
    use_report = opt != NULL && opt->map == NULL && opt->profile == KI_PROFILE_REPORT;
    if (use_report) {
        report_init(&report_state, opt->record_offset, opt->record_limit);
    }

    int failed = 0;
    for (;;) {
        if (opt != NULL && opt->cancelled != NULL && opt->cancelled(opt->cancel_user)) {
            rc = XML2PDF_ERR_RESOURCE;
            cause = KI_CAUSE_CANCEL;
            snprintf(msg, sizeof msg, "cancelled");
            say(opt, msg);
            failed = 1;
            break;
        }
        XmlEvent ev;
        int k = xml_next(&parser, &ev);
        if (k == XML_EV_ERROR) {
            if (reader.cancelled) {
                rc = XML2PDF_ERR_RESOURCE;
                cause = KI_CAUSE_CANCEL;
                snprintf(msg, sizeof msg, "cancelled");
            } else if (reader.io_failed) {
                rc = XML2PDF_ERR_IO;
                cause = KI_CAUSE_IO;
                snprintf(msg, sizeof msg, "read error");
            } else if (parser.pos_full) {
                rc = XML2PDF_ERR_RESOURCE;
                cause = KI_CAUSE_RESOURCE;
                xml_format_pos(msg, sizeof msg, "resource limit: input position", ev.line, ev.column, ev.byte_offset);
            } else {
                char why[sizeof msg];
                rc = XML2PDF_ERR_XML;
                cause = KI_CAUSE_XML;
                snprintf(why, sizeof why, "xml: %.150s", parser.errmsg);
                xml_format_pos(msg, sizeof msg, why, ev.line, ev.column, ev.byte_offset);
            }
            say(opt, msg);
            failed = 1;
            break;
        }
        if (k == XML_EV_EOF) {
            break;
        }
        KiEvent kev;
        memset(&kev, 0, sizeof kev);
        kev.kind = (int)ev.kind;
        kev.name = ev.name;
        kev.nattr = ev.nattr > KI_EVENT_ATTRS ? KI_EVENT_ATTRS : ev.nattr;
        for (int ai = 0; ai < kev.nattr; ai++) {
            kev.attr_name[ai] = ev.attr_name[ai];
            kev.attr_val[ai] = ev.attr_val[ai];
        }
        kev.text = ev.text;
        kev.text_len = ev.text_len;
        kev.from_cdata = ev.from_cdata;
        kev.byte_offset = ev.byte_offset;
        doc.line = ev.line;
        doc.column = ev.column;
        doc.byte = ev.byte_offset;
        int mapped;
        if (opt != NULL && opt->map != NULL) {
            mapped = opt->map(opt->map_user, &doc, &kev);
        } else if (use_report) {
            mapped = report_map(&report_state, &doc, &kev);
        } else {
            mapped = map_event(&lay, &map, &ev);
        }
        if (mapped != 0) {
            const char *why = NULL;
            if (doc.error[0] != '\0') {
                cause = doc.cause != 0 ? doc.cause : KI_CAUSE_PROFILE;
                rc = cause == KI_CAUSE_RESOURCE ? XML2PDF_ERR_RESOURCE : XML2PDF_ERR_XML;
                why = doc.error;
            } else {
                rc = fail_from_pdf(&pdf, &cause, &why);
                if (pdf.fail_kind == 0 && !pdf.hit_limit) {
                    if (lay.bad_cp != 0) {
                        rc = XML2PDF_ERR_XML;
                        cause = KI_CAUSE_FONT;
                        snprintf(glyph_msg, sizeof glyph_msg, "font: U+%04X", lay.bad_cp);
                        why = glyph_msg;
                    } else if (lay.line_full || lay.too_wide) {
                        rc = XML2PDF_ERR_RESOURCE;
                        cause = KI_CAUSE_RESOURCE;
                        why = lay.line_full ? LINE_FULL_MSG : TOO_WIDE_MSG;
                    } else {
                        rc = XML2PDF_ERR_XML;
                        cause = KI_CAUSE_XML;
                        why = "page write failed";
                    }
                }
            }
            snprintf(msg, sizeof msg, "%s", why);
            say(opt, msg);
            failed = 1;
            break;
        }
    }
    if (!failed && layout_finish(&lay) != 0) {
        const char *why = NULL;
        rc = fail_from_pdf(&pdf, &cause, &why);
        if (pdf.fail_kind == 0 && !pdf.hit_limit) {
            if (lay.bad_cp != 0) {
                rc = XML2PDF_ERR_XML;
                cause = KI_CAUSE_FONT;
                snprintf(glyph_msg, sizeof glyph_msg, "font: U+%04X", lay.bad_cp);
                why = glyph_msg;
            } else {
                rc = XML2PDF_ERR_XML;
                cause = KI_CAUSE_XML;
                why = "page flush failed";
            }
        }
        snprintf(msg, sizeof msg, "%s", why);
        say(opt, msg);
        failed = 1;
    }
    if (!failed && pdf_page_count(&pdf) == 0) {
        snprintf(msg, sizeof msg, "empty document");
        say(opt, msg);
        failed = 1;
    }
    if (!failed && opt != NULL && opt->cancelled != NULL && opt->cancelled(opt->cancel_user)) {
        rc = XML2PDF_ERR_RESOURCE;
        cause = KI_CAUSE_CANCEL;
        snprintf(msg, sizeof msg, "cancelled");
        say(opt, msg);
        failed = 1;
    }
    if (!failed && pdf_end(&pdf) != 0) {
        const char *why = NULL;
        rc = fail_from_pdf(&pdf, &cause, &why);
        if (pdf.fail_kind == 0 && !pdf.hit_limit) {
            why = "pdf end failed";
            rc = XML2PDF_ERR_IO;
            cause = KI_CAUSE_IO;
        }
        snprintf(msg, sizeof msg, "%s", why);
        say(opt, msg);
        failed = 1;
    }
    if (pdf_ready) {
        pages = pdf_page_count(&pdf);
        objects = pdf_object_count(&pdf);
        spool = pdf.spool_bytes;
        if (!failed && opt != NULL && opt->cancelled != NULL && opt->cancelled(opt->cancel_user)) {
            rc = XML2PDF_ERR_RESOURCE;
            cause = KI_CAUSE_CANCEL;
            snprintf(msg, sizeof msg, "cancelled");
            say(opt, msg);
            failed = 1;
        }
        if (!failed && pdf.fp != NULL && fflush(pdf.fp) != 0) {
            rc = XML2PDF_ERR_IO;
            cause = KI_CAUSE_IO;
            snprintf(msg, sizeof msg, "pdf publish failed");
            say(opt, msg);
            failed = 1;
        }
        /* Closing the writer stream can still pass buffered bytes through
           the accounting layer after a failure; count them too. */
        pdf_free(&pdf);
        written = pdf.written;
        pdf_ready = 0;
    }
    if (!failed && out != NULL) {
        int publish_fail = fail_before_publish;
        int fd = fileno(out);
        int sync_fail = fflush(out) != 0 || ferror(out) || publish_fail;
        if (!sync_fail && opt != NULL && opt->cancelled != NULL && opt->cancelled(opt->cancel_user)) {
            rc = XML2PDF_ERR_RESOURCE;
            cause = KI_CAUSE_CANCEL;
            snprintf(msg, sizeof msg, "cancelled");
            say(opt, msg);
            failed = 1;
            sync_fail = 1;
        }
        if (!sync_fail && fd >= 0 && fsync(fd) != 0) {
            sync_fail = 1;
        }
        if (!sync_fail && opt != NULL && opt->cancelled != NULL && opt->cancelled(opt->cancel_user)) {
            rc = XML2PDF_ERR_RESOURCE;
            cause = KI_CAUSE_CANCEL;
            snprintf(msg, sizeof msg, "cancelled");
            say(opt, msg);
            failed = 1;
            sync_fail = 1;
        }
        if (sync_fail && cause != KI_CAUSE_CANCEL) {
            rc = XML2PDF_ERR_IO;
            cause = KI_CAUSE_IO;
            snprintf(msg, sizeof msg, "pdf publish failed");
            say(opt, msg);
            failed = 1;
        }
    }
    if (out != NULL) {
        if (fclose(out) != 0 && !failed) {
            rc = XML2PDF_ERR_IO;
            cause = KI_CAUSE_IO;
            snprintf(msg, sizeof msg, "pdf close failed");
            say(opt, msg);
            failed = 1;
        }
        out = NULL;
    }
    if (!failed && opt != NULL && opt->cancelled != NULL && opt->cancelled(opt->cancel_user)) {
        rc = XML2PDF_ERR_RESOURCE;
        cause = KI_CAUSE_CANCEL;
        snprintf(msg, sizeof msg, "cancelled");
        say(opt, msg);
        failed = 1;
    }
    if (!failed) {
        if (tmp_path != NULL) {
            if (rename(tmp_path, pdf_path) != 0) {
                rc = XML2PDF_ERR_IO;
                cause = KI_CAUSE_IO;
                snprintf(msg, sizeof msg, "cannot replace output %s", pdf_path);
                say(opt, msg);
                failed = 1;
            } else {
                published = 1;
            }
        } else {
            published = 1;
        }
        if (published) {
            snprintf(msg, sizeof msg, "xml2pdf: pages=%d objects=%d offset_bytes=%ld", pages, objects, spool);
            say(opt, msg);
            rc = XML2PDF_OK;
            cause = KI_CAUSE_NONE;
        }
    }

done:
    if (pdf_ready) {
        pdf_free(&pdf);
        written = pdf.written;
        pdf_ready = 0;
    }
    if (result != NULL) {
        result->status = published ? XML2PDF_OK : rc;
        result->cause = cause;
        snprintf(result->message, sizeof result->message, "%s", msg);
        result->pages = pages;
        result->objects = objects;
        result->offset_bytes = spool;
        result->output_bytes = written;
        result->spool_bytes = spool;
        if (use_report) {
            result->report_total = report_state.total;
            result->report_selected = report_state.selected;
            result->report_skipped = report_state.total - report_state.selected;
        }
    }
    if (out != NULL) {
        fclose(out);
    }
    if (in != NULL) {
        fclose(in);
    }
    if (tmp_path != NULL && !published) {
        unlink(tmp_path);
    }
    free(tmp_path);
    return rc;
}

typedef struct {
    int (*write_bytes)(void *user, const char *src, int n);
    void *user;
    long accepted;
    int failed;
} ByteSink;

/* The writer counts bytes as it hands them to stdio, but stdio buffers
   them; only what the callback accepted is the delivered output.
   stdio drops a buffer whose write failed and lets later writes succeed,
   so the first failure is latched: the callback is never called again and
   the stream stays in error, which the converter checks before success. */
static ssize_t sink_write(void *cookie, const char *buf, size_t n) {
    ByteSink *sink = cookie;
    if (sink->failed || n > 2147483647u) {
        sink->failed = 1;
        errno = EIO;
        return -1;
    }
    int wr = sink->write_bytes(sink->user, buf, (int)n);
    if (wr > 0 && (size_t)wr <= n) {
        sink->accepted += wr;
    }
    if (wr < 0 || (size_t)wr != n) {
        sink->failed = 1;
        errno = EIO;
        return -1;
    }
    return (ssize_t)wr;
}

static int sink_close(void *cookie) {
    (void)cookie;
    return 0;
}

int ki_convert(const KiOptions *opt, KiResult *result) {
    return ki_convert_cfg(opt, NULL, result);
}

int ki_convert_cfg(const KiOptions *opt, const KiConfig *cfg, KiResult *result) {
    if (result != NULL) {
        memset(result, 0, sizeof *result);
    }
    Setup setup;
    if (reject_options(opt, result) != 0 || reject_config(cfg, opt, &setup, result) != 0) {
        return XML2PDF_ERR_USAGE;
    }
    if (opt == NULL || opt->read_bytes == NULL || opt->write_bytes == NULL) {
        if (result != NULL) {
            memset(result, 0, sizeof *result);
            result->status = XML2PDF_ERR_USAGE;
            result->cause = KI_CAUSE_USAGE;
            snprintf(result->message, sizeof result->message, "read and write callbacks are required");
        }
        return XML2PDF_ERR_USAGE;
    }
    ByteSink *sink = malloc(sizeof *sink);
    if (sink == NULL) {
        if (result != NULL) {
            memset(result, 0, sizeof *result);
            result->status = XML2PDF_ERR_RESOURCE;
            result->cause = KI_CAUSE_RESOURCE;
            snprintf(result->message, sizeof result->message, "out of memory");
        }
        return XML2PDF_ERR_RESOURCE;
    }
    sink->write_bytes = opt->write_bytes;
    sink->user = opt->io_user;
    sink->accepted = 0;
    sink->failed = 0;
    cookie_io_functions_t fn;
    memset(&fn, 0, sizeof fn);
    fn.write = sink_write;
    fn.close = sink_close;
    FILE *out = fopencookie(sink, "w", fn);
    if (out == NULL) {
        free(sink);
        if (result != NULL) {
            memset(result, 0, sizeof *result);
            result->status = XML2PDF_ERR_IO;
            result->cause = KI_CAUSE_IO;
            snprintf(result->message, sizeof result->message, "cannot open write callback");
        }
        return XML2PDF_ERR_IO;
    }
    /* Reuse the file converter's body by pointing it at this stream.
       There is no destination path, so nothing is renamed into place.
       convert_streams always closes the stream, so the sink outlives it. */
    int rc = convert_streams(NULL, out, NULL, NULL, opt, &setup, result);
    if (result != NULL) {
        result->output_bytes = sink->accepted;
    }
    free(sink);
    return rc;
}

static void log_stderr(void *user, const char *msg) {
    (void)user;
    fprintf(stderr, "%s\n", msg);
}

int xml2pdf_convert_cap(const char *xml_path, const char *pdf_path, size_t page_cap) {
    KiOptions opt;
    ki_options_init(&opt);
    opt.log = log_stderr;
    opt.max_output_bytes = page_cap > 0 ? (long)page_cap : 0;
    KiResult result;
    int rc = ki_convert_file(xml_path, pdf_path, &opt, &result);
    fail_at_page = 0;
    fail_before_publish = 0;
    fail_after_bytes = 0;
    return rc;
}

int xml2pdf_convert(const char *xml_path, const char *pdf_path) {
    return xml2pdf_convert_cap(xml_path, pdf_path, 0);
}
