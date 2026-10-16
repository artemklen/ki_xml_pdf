#include "ki_xml_pdf.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t cli_stop;

static void on_stop(int sig) {
    (void)sig;
    cli_stop = 1;
}

static int cli_cancelled(void *user) {
    (void)user;
    return cli_stop != 0;
}

typedef struct {
    FILE *input;
    const char *path;
    int open_failed;
} CliIo;

static void cli_log(void *user, const char *msg) {
    const CliIo *io = user;
    /* The open failure is reported once, by name, after the conversion. */
    if (io->open_failed) {
        return;
    }
    fprintf(stderr, "%s\n", msg);
}

static int cli_read(void *user, char *dst, int max) {
    CliIo *io = user;
    /* Opened on the first read so that rejected options never touch the input. */
    if (io->input == NULL) {
        io->input = fopen(io->path, "rb");
        if (io->input == NULL) {
            io->open_failed = 1;
            return -1;
        }
    }
    errno = 0;
    size_t n = fread(dst, 1, (size_t)max, io->input);
    if (ferror(io->input) || (n == 0 && errno == EINTR)) {
        return -1;
    }
    return (int)n;
}

static int cli_write(void *user, const char *src, int n) {
    (void)user;
    return fwrite(src, 1, (size_t)n, stdout) == (size_t)n ? n : -1;
}

static void usage(FILE *fp) {
    fprintf(fp,
            "xml2pdf %s — profiles %s, %s\n"
            "usage: xml2pdf [options] [--] input.xml output.pdf\n"
            "       xml2pdf -h | --help | --version\n"
            "Use '-' for stdin or stdout. Conversion diagnostics go to stderr.\n"
            "\n"
            "Options:\n"
            "  --profile NAME           records-v1 (default) or report-v1\n"
            "  --record-offset N        skip N report-v1 records\n"
            "  --record-limit N         select N records after offset (0: all)\n"
            "  --font PATH              use only this font, with no fallback\n"
            "  --max-pages N            page quota (0: no separate quota)\n"
            "  --max-output-bytes N     PDF byte quota (0: only the 9999999999-byte xref ceiling)\n"
            "  --max-spool-bytes N      offset-file quota (0: 64 MiB)\n"
            "  --temp-dir DIR           offset-file directory\n"
            "\n"
            "Page geometry (points, 1/72 inch; decimal digits only, e.g. 24 or 595.276):\n"
            "  --page-size NAME         letter (default, 612x792) or a4 (595.276x841.89)\n"
            "  --orientation NAME       portrait (default) or landscape for --page-size\n"
            "  --page-width PT          custom width 3..14400, with --page-height\n"
            "  --page-height PT         custom height 3..14400, with --page-width\n"
            "  --margin PT|T,R,B,L      all margins, or top,right,bottom,left (default 40)\n"
            "  --font-size PT           1..144 (default 10); leading is 1.4 x size\n"
            "Each geometry option may appear once. A custom size excludes --page-size\n"
            "and --orientation. Invalid geometry exits 2 before any input is read.\n"
            "\n"
            "Document information (/Info), each optional and written only when given:\n"
            "  --title TEXT             UTF-8, 1..1024 bytes; no controls except tab, LF, CR\n"
            "  --producer TEXT          same rules as --title\n"
            "  --creation-date DATE     YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS+HH:MM/-HH:MM\n"
            "No date is added unless given; no XMP or PDF/A claim. Invalid metadata\n"
            "exits 2 before any input is read or output created.\n"
            "\n"
            "Input: UTF-8; no network access. XML bounds: depth 64, 8 attributes,\n"
            "names 63 bytes, attribute values 255 bytes, input window 4096 bytes.\n"
            "Exit codes: 0 success, 1 XML/profile/font, 2 usage, 3 I/O, 4 quota/cancel.\n"
            "Files are replaced atomically; stdout may contain a partial PDF on failure.\n",
            KI_XML_PDF_VERSION, KI_XML_PDF_PROFILE_RECORDS, KI_XML_PDF_PROFILE_REPORT);
}

/* Plain decimal points only: no sign, exponent, hex, inf/nan or spaces. */
static int take_points(const char *text, double *out) {
    size_t n = strlen(text);
    size_t digits = strspn(text, "0123456789");
    if (n == 0 || n > 32 || digits == 0) {
        return -1;
    }
    if (text[digits] == '.') {
        size_t frac = strspn(text + digits + 1, "0123456789");
        if (frac == 0 || digits + 1 + frac != n) {
            return -1;
        }
    } else if (digits != n) {
        return -1;
    }
    char *end = NULL;
    errno = 0;
    *out = strtod(text, &end);
    return end != NULL && *end == '\0' && errno == 0 ? 0 : -1;
}

/* One value for all four margins, or top,right,bottom,left. */
static int take_margins(const char *text, double margin[4]) {
    char part[33];
    int count = 0;
    const char *p = text;
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);
        if (count == 4 || len >= sizeof part) {
            return -1;
        }
        memcpy(part, p, len);
        part[len] = '\0';
        if (take_points(part, &margin[count]) != 0) {
            return -1;
        }
        count++;
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    if (count == 1) {
        margin[1] = margin[2] = margin[3] = margin[0];
        return 0;
    }
    return count == 4 ? 0 : -1;
}

static int take_long(const char *text, long *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (text[0] == '\0' || end == NULL || *end != '\0' || errno != 0 || v < 0) {
        return -1;
    }
    *out = v;
    return 0;
}

int main(int argc, char **argv) {
    const char *font = NULL;
    const char *temp_dir = NULL;
    const char *profile = KI_XML_PDF_PROFILE_RECORDS;
    long max_pages = 0;
    long max_output = 0;
    long max_spool = 0;
    long record_offset = 0;
    long record_limit = 0;
    KiConfig cfg;
    ki_config_init(&cfg, sizeof cfg);
    int seen_size = 0, seen_orient = 0, seen_width = 0, seen_height = 0, seen_margin = 0, seen_font = 0;
    int seen_title = 0, seen_producer = 0, seen_date = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("xml2pdf %s %s\n", KI_XML_PDF_VERSION, KI_XML_PDF_PROFILE);
            return 0;
        }
        if (i + 1 >= argc) {
            usage(stderr);
            return 2;
        }
        if (strcmp(argv[i], "--profile") == 0) {
            profile = argv[++i];
        } else if (strcmp(argv[i], "--record-offset") == 0) {
            if (take_long(argv[++i], &record_offset) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--record-limit") == 0) {
            if (take_long(argv[++i], &record_limit) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--font") == 0) {
            font = argv[++i];
        } else if (strcmp(argv[i], "--temp-dir") == 0) {
            temp_dir = argv[++i];
        } else if (strcmp(argv[i], "--max-pages") == 0) {
            if (take_long(argv[++i], &max_pages) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--max-output-bytes") == 0) {
            if (take_long(argv[++i], &max_output) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--max-spool-bytes") == 0) {
            if (take_long(argv[++i], &max_spool) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--page-size") == 0) {
            const char *v = argv[++i];
            if (seen_size++ || (strcmp(v, "letter") != 0 && strcmp(v, "a4") != 0)) {
                usage(stderr);
                return 2;
            }
            cfg.page_size = strcmp(v, "a4") == 0 ? KI_PAGE_A4 : KI_PAGE_LETTER;
        } else if (strcmp(argv[i], "--orientation") == 0) {
            const char *v = argv[++i];
            if (seen_orient++ || (strcmp(v, "portrait") != 0 && strcmp(v, "landscape") != 0)) {
                usage(stderr);
                return 2;
            }
            cfg.orientation = strcmp(v, "landscape") == 0 ? KI_ORIENT_LANDSCAPE : KI_ORIENT_PORTRAIT;
        } else if (strcmp(argv[i], "--page-width") == 0) {
            if (seen_width++ || take_points(argv[++i], &cfg.page_width) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--page-height") == 0) {
            if (seen_height++ || take_points(argv[++i], &cfg.page_height) != 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--margin") == 0) {
            double m[4];
            if (seen_margin++ || take_margins(argv[++i], m) != 0) {
                usage(stderr);
                return 2;
            }
            cfg.margin_top = m[0];
            cfg.margin_right = m[1];
            cfg.margin_bottom = m[2];
            cfg.margin_left = m[3];
        } else if (strcmp(argv[i], "--title") == 0) {
            if (seen_title++) {
                usage(stderr);
                return 2;
            }
            cfg.title = argv[++i];
        } else if (strcmp(argv[i], "--producer") == 0) {
            if (seen_producer++) {
                usage(stderr);
                return 2;
            }
            cfg.producer = argv[++i];
        } else if (strcmp(argv[i], "--creation-date") == 0) {
            if (seen_date++) {
                usage(stderr);
                return 2;
            }
            cfg.creation_date = argv[++i];
        } else if (strcmp(argv[i], "--font-size") == 0) {
            if (seen_font++ || take_points(argv[++i], &cfg.font_size) != 0) {
                usage(stderr);
                return 2;
            }
        } else {
            usage(stderr);
            return 2;
        }
        i++;
    }
    if (argc - i != 2) {
        usage(stderr);
        return 2;
    }

    if (max_pages > INT_MAX || seen_width != seen_height || (seen_width && (seen_size || seen_orient))) {
        usage(stderr);
        return 2;
    }
    if (seen_width) {
        cfg.page_size = KI_PAGE_CUSTOM;
    }
    KiOptions opt;
    ki_options_init(&opt);
    opt.font_path = font;
    opt.record_offset = record_offset;
    opt.record_limit = record_limit;
    if (strcmp(profile, KI_XML_PDF_PROFILE_REPORT) == 0) {
        opt.profile = KI_PROFILE_REPORT;
    } else if (strcmp(profile, KI_XML_PDF_PROFILE_RECORDS) != 0) {
        usage(stderr);
        return 2;
    }
    opt.temp_dir = temp_dir;
    opt.max_pages = (int)max_pages;
    opt.max_output_bytes = max_output;
    opt.max_spool_bytes = max_spool;
    opt.cancelled = cli_cancelled;
    opt.log = cli_log;
    const char *input_path = argv[i];
    const char *output_path = argv[i + 1];
    int input_stream = strcmp(input_path, "-") == 0;
    int output_stream = strcmp(output_path, "-") == 0;
    CliIo io;
    io.input = input_stream ? stdin : NULL;
    io.path = input_path;
    io.open_failed = 0;
    opt.log_user = &io;
    if (input_stream || output_stream) {
        opt.read_bytes = cli_read;
        opt.write_bytes = cli_write;
        opt.io_user = &io;
    }
    if (output_stream) {
        /* Surface errors during the conversion, before reporting success. */
        setvbuf(stdout, NULL, _IONBF, 0);
    }
    struct sigaction sa, old_int, old_term, old_pipe;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    int installed_int = sigaction(SIGINT, &sa, &old_int) == 0;
    int installed_term = sigaction(SIGTERM, &sa, &old_term) == 0;
    sa.sa_handler = SIG_IGN;
    int installed_pipe = output_stream && sigaction(SIGPIPE, &sa, &old_pipe) == 0;

    KiResult result;
    int rc;
    if (output_stream) {
        rc = ki_convert_cfg(&opt, &cfg, &result);
        if (rc == 0 && fflush(stdout) != 0) {
            fprintf(stderr, "stdout write failed\n");
            rc = 3;
        }
    } else if (input_stream) {
        rc = ki_convert_to_file_cfg(output_path, &opt, &cfg, &result);
    } else {
        rc = ki_convert_file_cfg(input_path, output_path, &opt, &cfg, &result);
    }
    if (io.input != NULL && io.input != stdin) {
        fclose(io.input);
    }
    if (io.open_failed) {
        fprintf(stderr, "cannot open input %s\n", input_path);
        rc = 3;
    }
    if (installed_int) sigaction(SIGINT, &old_int, NULL);
    if (installed_term) sigaction(SIGTERM, &old_term, NULL);
    if (installed_pipe) sigaction(SIGPIPE, &old_pipe, NULL);
    return rc;
}
