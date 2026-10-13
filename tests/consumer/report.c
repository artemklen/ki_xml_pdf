#include "ki_xml_pdf.h"

#include <stdio.h>
#include <string.h>

#ifndef KI_EXPECTED_VERSION
#define KI_EXPECTED_VERSION KI_XML_PDF_VERSION
#endif

int main(int argc, char **argv) {
    if (argc != 3 || strcmp(KI_XML_PDF_VERSION, KI_EXPECTED_VERSION) != 0) {
        fprintf(stderr, "consumer: arguments or installed version mismatch\n");
        return 2;
    }
    KiOptions options;
    KiResult result;
    ki_options_init(&options);
    options.profile = KI_PROFILE_REPORT;
    int rc = ki_convert_file(argv[1], argv[2], &options, &result);
    if (rc != 0) {
        fprintf(stderr, "consumer: %s\n", result.message);
        return rc;
    }
    if (result.pages != 1 || result.report_total != 2 || result.report_selected != 2 ||
        result.report_skipped != 0) {
        fprintf(stderr, "consumer: report counters mismatch\n");
        return 1;
    }
    printf("consumer %s: pages=%d total=%llu selected=%llu skipped=%llu\n",
           KI_XML_PDF_VERSION, result.pages, result.report_total, result.report_selected,
           result.report_skipped);
    return 0;
}
