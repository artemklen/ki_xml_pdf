/* SPDX-License-Identifier: MIT */
#ifndef CATALOG_MAP_H
#define CATALOG_MAP_H

#include "ki_xml_pdf.h"

/* One enum, one flag and one counter; no field or item storage. */
typedef struct {
    int state;
    int value_started;
    unsigned long long items;
} CatalogMap;

void catalog_map_init(CatalogMap *map);
int catalog_map_event(void *user, KiDoc *doc, const KiEvent *event);

#endif

