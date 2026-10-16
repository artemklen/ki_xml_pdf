/* SPDX-License-Identifier: MIT */
#ifndef KI_XML_PDF_H
#define KI_XML_PDF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public conversion API. One context is the call's options and result.
   The library does not install signal handlers, does not read the
   environment except KI_XML_PDF_FONT when no font path is set, and does
   not write to the terminal unless the caller supplies a log callback.
   A context is not safe for concurrent use. Two contexts may run at once.
   Call ki_options_init before filling KiOptions. size must stay equal to
   sizeof(KiOptions); a mismatch is a usage error.
   The 0.x API is unstable until 1.0: a 0.y bump may change this struct.
   Default font after install is PREFIX/share/ki_xml_pdf/DejaVuSans.ttf,
   compiled into the library. DESTDIR only stages files; it does not change
   that runtime path. An explicit font_path or KI_XML_PDF_FONT is not replaced
   if it fails. Cancellation is checked before publication, about every
   64 objects while the PDF is finished, and again after fsync and close
   before rename. After a successful rename, cancel does not roll the file
   back. A character outside the font repertoire is KI_CAUSE_FONT with the
   U+ code point, not an XML syntax error. A zero-advance glyph (combining
   mark) is present. Text wraps at spaces and tabs; a longer word is cut at
   a code point boundary. One base with zero-advance marks over 1023 UTF-8
   bytes cannot be cut and is KI_CAUSE_RESOURCE. XML and profile failures, including ki_doc_fail, end
   with "at line L, column C, byte B": the consumed position, line/column
   1-based in Unicode scalars, byte 0-based in raw input bytes (BOM
   included, CR/LF/CRLF one line break, a long rule shortened at a UTF-8
   boundary). Other causes carry no position. ki_convert leaves bytes
   already accepted by write_bytes in place when it fails. */

#define KI_XML_PDF_VERSION "0.2.0"
#define KI_XML_PDF_PROFILE_RECORDS "records-v1"
#define KI_XML_PDF_PROFILE_REPORT "report-v1"
#define KI_XML_PDF_PROFILE KI_XML_PDF_PROFILE_RECORDS

#if defined(__GNUC__)
#define KI_API __attribute__((visibility("default")))
#else
#define KI_API
#endif

enum {
    KI_CAUSE_NONE = 0,
    KI_CAUSE_XML = 1,
    KI_CAUSE_USAGE = 2,
    KI_CAUSE_IO = 3,
    KI_CAUSE_RESOURCE = 4,
    KI_CAUSE_FONT = 5,
    KI_CAUSE_CANCEL = 6,
    KI_CAUSE_PROFILE = 7
};

enum {
    KI_PROFILE_RECORDS = 0,
    KI_PROFILE_REPORT = 1
};

enum {
    KI_EV_START = 1,
    KI_EV_END = 2,
    KI_EV_TEXT = 3
};

#define KI_EVENT_ATTRS 8

typedef struct KiDoc KiDoc;

/* Pointers are valid only for the duration of the map callback. */
typedef struct KiEvent {
    int kind;
    const char *name;
    int nattr;
    const char *attr_name[KI_EVENT_ATTRS];
    const char *attr_val[KI_EVENT_ATTRS];
    const char *text;
    size_t text_len;
    int from_cdata;
    /* Parser position after this event's bytes were consumed. An empty
       element's synthetic end uses that same position. EOF is the position
       at the end of the input. */
    size_t byte_offset;
} KiEvent;

typedef struct KiOptions {
    size_t size;
    const char *font_path;
    const char *temp_dir;
    int max_pages;
    long max_output_bytes;
    long max_spool_bytes;
    int profile;
    long record_offset;
    long record_limit;
    int (*cancelled)(void *user);
    void *cancel_user;
    int (*map)(void *user, KiDoc *doc, const KiEvent *ev);
    void *map_user;
    void (*log)(void *user, const char *msg);
    void *log_user;
    /* Callback I/O for ki_convert and ki_convert_to_file. read_bytes returns
       0 only at EOF, 1..max for data (a short read is not EOF), or <0 for an
       error. write_bytes must return n after accepting the entire count or
       <0 for an error; a short write also fails. Accepted bytes are not
       rolled back. Callback state/resources belong to the caller and are
       never closed/freed by the library. ki_convert_file ignores both
       callbacks; ki_convert_to_file ignores write_bytes. */
    int (*read_bytes)(void *user, char *dst, int max);
    int (*write_bytes)(void *user, const char *src, int n);
    void *io_user;
} KiOptions;

typedef struct KiResult {
    int status;
    int cause;
    char message[160];
    int pages;
    int objects;
    long offset_bytes;
    long output_bytes;
    long spool_bytes;
    unsigned long long report_total;
    unsigned long long report_selected;
    unsigned long long report_skipped;
} KiResult;

KI_API void ki_options_init(KiOptions *opt);
KI_API int ki_doc_text(KiDoc *doc, const char *s, size_t n);
KI_API int ki_doc_break(KiDoc *doc);
KI_API int ki_doc_fail(KiDoc *doc, const char *rule);
KI_API int ki_convert_file(const char *xml_path, const char *pdf_path, const KiOptions *opt,
                           KiResult *result);
KI_API int ki_convert(const KiOptions *opt, KiResult *result);
/* Callback input to an atomic file destination. opt and read_bytes are
   required; write_bytes is ignored. Uses the same temporary-file, full input
   validation, flush/fsync/close and rename lifecycle as ki_convert_file.
   Every call initializes a non-NULL result, including early usage errors.
   The previous destination is preserved on failure and the temp is removed.
   The caller retains ownership of input/callback resources. */
KI_API int ki_convert_to_file(const char *pdf_path, const KiOptions *opt, KiResult *result);

/* Page geometry, added beside KiOptions so that KiOptions, KiResult and
   KiEvent keep their 0.2 layout. Values are PostScript points (1/72 inch),
   rounded to the nearest 0.001 pt; the PDF MediaBox and text placement use
   the same rounded values. Letter is 612 x 792, A4 595.276 x 841.89 (210 x
   297 mm); landscape swaps width and height of a named size. A custom size
   sets page_width/page_height and stays as given (orientation must be
   portrait); a named size requires both to be 0. Limits: page sides 3..14400,
   margins 0..14400 with left + right < width and top + bottom < height, font
   size 1..144. NaN and infinities are rejected. Leading is 1.4 x font size
   (14 pt at 10 pt), rounded down to 0.001 pt. A page holds at most 50 lines.
   Every glyph's ink, not only its advance, stays inside the margins: a line
   whose first glyph extends left of its origin starts further right, and the
   first baseline and the line count leave room for the font's ascent and
   descent. An invalid value fails with KI_CAUSE_USAGE before any file is
   opened or callback runs. A valid geometry with no room for one line, or
   for some single glyph of the loaded font, fails with KI_CAUSE_USAGE after
   the font loads and before any read, write or map callback; a file
   destination is left as it was. */
enum {
    KI_PAGE_LETTER = 0,
    KI_PAGE_A4 = 1,
    KI_PAGE_CUSTOM = 2
};

enum {
    KI_ORIENT_PORTRAIT = 0,
    KI_ORIENT_LANDSCAPE = 1
};

typedef struct KiConfig {
    size_t size;
    int page_size;
    int orientation;
    double page_width;
    double page_height;
    double margin_top;
    double margin_right;
    double margin_bottom;
    double margin_left;
    double font_size;
} KiConfig;

/* Writes defaults (Letter portrait, 40 pt margins, 10 pt font) when size is
   a KiConfig size this library knows, writing no more than size bytes, and
   returns 0. Otherwise writes nothing and returns 2. Use
   ki_config_init(&cfg, sizeof cfg). A conversion rejects any other
   cfg->size as KI_CAUSE_USAGE. */
KI_API int ki_config_init(KiConfig *cfg, size_t size);
/* As ki_convert_file, ki_convert and ki_convert_to_file, with cfg's page
   geometry. A NULL cfg is the default geometry, identical to the 0.2 calls. */
KI_API int ki_convert_file_cfg(const char *xml_path, const char *pdf_path, const KiOptions *opt, const KiConfig *cfg,
                               KiResult *result);
KI_API int ki_convert_cfg(const KiOptions *opt, const KiConfig *cfg, KiResult *result);
KI_API int ki_convert_to_file_cfg(const char *pdf_path, const KiOptions *opt, const KiConfig *cfg, KiResult *result);

#ifdef __cplusplus
}
#endif

#endif
