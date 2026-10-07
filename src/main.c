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

static void usage(FILE *fp) {
    fprintf(fp,
            "xml2pdf %s — профили %s, %s\n"
            "использование: xml2pdf [опции] input.xml output.pdf\n"
            "               xml2pdf --help\n"
            "               xml2pdf --version\n"
            "\n"
            "Опции:\n"
            "  --profile NAME           records-v1 (по умолчанию) или report-v1\n"
            "  --record-offset N        пропустить N записей report-v1\n"
            "  --record-limit N         взять N записей после offset (0 — остальные)\n"
            "  --font PATH              только этот шрифт, без подмены\n"
            "  --max-pages N            потолок страниц (0 — без отдельного потолка)\n"
            "  --max-output-bytes N     потолок байт PDF\n"
            "  --max-spool-bytes N      потолок файла смещений (0 — 64 МиБ)\n"
            "  --temp-dir DIR           каталог файла смещений\n"
            "\n"
            "Кодировка входа: UTF-8. Сеть не используется.\n"
            "Документ records-v1: имя элемента, строки «атрибут: значение», текст.\n"
            "Лимиты: глубина 64, атрибутов 8, имя 63 байта, значение 255 байт,\n"
            "окно ввода 4096 байт, не больше 8000000 объектов PDF.\n"
            "Коды: 0 успех, 1 XML или шрифт, 2 вызов, 3 ввод-вывод, 4 ресурс или отмена.\n"
            "Причина отмены и шрифта видна в тексте ошибки, не только в коде.\n",
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
    while (i < argc && strncmp(argv[i], "--", 2) == 0) {
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

    struct sigaction sa;
    struct sigaction old_int;
    struct sigaction old_term;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);

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
    KiResult result;
    int rc = ki_convert_file(argv[i], argv[i + 1], &opt, &result);
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    return rc;
}
