#include "ki_xml_pdf.h"

#include <stdio.h>

/* xml2pdf --profile report-v1 is the CLI. This file is the same choice through the API. */

int main(int argc, char **argv) {
    const char *in = argc > 1 ? argv[1] : "examples/report-v1.xml";
    const char *out = argc > 2 ? argv[2] : "report.pdf";
    KiOptions opt;
    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    KiResult result;
    int rc = ki_convert_file(in, out, &opt, &result);
    if (rc != 0) {
        fprintf(stderr, "report: %s\n", result.message);
        return rc;
    }
    printf("pages=%d total=%llu selected=%llu skipped=%llu\n", result.pages, result.report_total,
           result.report_selected, result.report_skipped);
    return 0;
}
