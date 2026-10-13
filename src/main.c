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

static void cli_log(void *user, const char *msg) {
    (void)user;
    fprintf(stderr, "%s\n", msg);
}

typedef struct {
    FILE *input;
} CliIo;

static int cli_read(void *user, char *dst, int max) {
    CliIo *io = user;
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
            "  --max-output-bytes N     PDF byte quota\n"
            "  --max-spool-bytes N      offset-file quota (0: 64 MiB)\n"
            "  --temp-dir DIR           offset-file directory\n"
            "\n"
            "Input: UTF-8; no network access. XML bounds: depth 64, 8 attributes,\n"
            "names 63 bytes, attribute values 255 bytes, input window 4096 bytes.\n"
            "Exit codes: 0 success, 1 XML/profile/font, 2 usage, 3 I/O, 4 quota/cancel.\n"
            "Files are replaced atomically; stdout may contain a partial PDF on failure.\n",
            KI_XML_PDF_VERSION, KI_XML_PDF_PROFILE_RECORDS, KI_XML_PDF_PROFILE_REPORT);
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

    if (max_pages > INT_MAX) {
        usage(stderr);
        return 2;
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
    io.input = NULL;
    if (input_stream || output_stream) {
        io.input = input_stream ? stdin : fopen(input_path, "rb");
        if (io.input == NULL) {
            fprintf(stderr, "cannot open input %s\n", input_path);
            return 3;
        }
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
        rc = ki_convert(&opt, &result);
        if (rc == 0 && fflush(stdout) != 0) {
            fprintf(stderr, "stdout write failed\n");
            rc = 3;
        }
    } else if (input_stream) {
        rc = ki_convert_to_file(output_path, &opt, &result);
    } else {
        rc = ki_convert_file(input_path, output_path, &opt, &result);
    }
    if (io.input != NULL && io.input != stdin) {
        fclose(io.input);
    }
    if (installed_int) sigaction(SIGINT, &old_int, NULL);
    if (installed_term) sigaction(SIGTERM, &old_term, NULL);
    if (installed_pipe) sigaction(SIGPIPE, &old_pipe, NULL);
    return rc;
}
