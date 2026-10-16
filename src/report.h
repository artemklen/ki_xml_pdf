#ifndef REPORT_H
#define REPORT_H

#include "ki_xml_pdf.h"

/* Fixed state for report-v1. It does not grow with the number of records. */

typedef struct Report {
    int in_report;
    int in_record;
    int in_field;
    int saw_field;
    int skipping;
    int value_started;
    unsigned long long index;
    unsigned long long total;
    unsigned long long selected;
    unsigned long long offset;
    unsigned long long limit;
    char pending_name[256];
} Report;

void report_init(Report *r, long offset, long limit);
int report_map(Report *r, KiDoc *doc, const KiEvent *ev);

#endif
