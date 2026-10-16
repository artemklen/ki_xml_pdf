/* SPDX-License-Identifier: MIT */
#include "catalog_map.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    BEFORE_CATALOG,
    IN_CATALOG,
    NEED_NAME,
    IN_NAME,
    NEED_PRICE,
    IN_PRICE,
    ITEM_DONE,
    AFTER_CATALOG
};

void catalog_map_init(CatalogMap *map) {
    memset(map, 0, sizeof *map);
}

static int only_space(const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return 0;
        }
    }
    return 1;
}

/* ki_doc_fail appends the line, column and byte of the current event. */
static int reject(KiDoc *doc, const char *rule) {
    char message[160];
    snprintf(message, sizeof message, "catalog: %s", rule);
    return ki_doc_fail(doc, message);
}

static int emit_line(KiDoc *doc, const char *text) {
    if (ki_doc_text(doc, text, strlen(text)) != 0) {
        return -1;
    }
    return ki_doc_break(doc);
}

static int check_attributes(KiDoc *doc, const KiEvent *event, const char *allowed) {
    for (int i = 0; i < event->nattr; i++) {
        const char *name = event->attr_name[i];
        if (strchr(name, ':') != NULL || strcmp(name, "xmlns") == 0) {
            return reject(doc, "namespaces are not supported");
        }
        if (allowed == NULL || strcmp(name, allowed) != 0) {
            char rule[112];
            snprintf(rule, sizeof rule, "unknown attribute %.63s on %.7s", name, event->name);
            return reject(doc, rule);
        }
    }
    return 0;
}

static int start_item(CatalogMap *map, KiDoc *doc, const KiEvent *event) {
    if (map->state != IN_CATALOG) {
        return reject(doc, "item nesting");
    }
    if (check_attributes(doc, event, "id") != 0) {
        return -1;
    }
    const char *id = event->nattr == 1 ? event->attr_val[0] : NULL;
    if (id != NULL && only_space(id, strlen(id))) {
        return reject(doc, "empty item id");
    }
    if (map->items == ULLONG_MAX) {
        return reject(doc, "item counter overflow");
    }
    map->items++;
    if (ki_doc_text(doc, "Item ", 5) != 0) {
        return -1;
    }
    if (id != NULL) {
        /* The attribute pointer is consumed here, within this callback. */
        if (ki_doc_text(doc, id, strlen(id)) != 0) {
            return -1;
        }
    } else {
        char number[32];
        snprintf(number, sizeof number, "%llu", map->items);
        if (ki_doc_text(doc, number, strlen(number)) != 0) {
            return -1;
        }
    }
    if (ki_doc_break(doc) != 0) {
        return -1;
    }
    map->state = NEED_NAME;
    return 0;
}

static int start_element(CatalogMap *map, KiDoc *doc, const KiEvent *event) {
    if (strchr(event->name, ':') != NULL) {
        return reject(doc, "namespaces are not supported");
    }
    if (strcmp(event->name, "catalog") == 0) {
        if (map->state != BEFORE_CATALOG) {
            return reject(doc, "catalog nesting");
        }
        if (check_attributes(doc, event, NULL) != 0 || emit_line(doc, "Catalog") != 0) {
            return -1;
        }
        map->state = IN_CATALOG;
        return 0;
    }
    if (strcmp(event->name, "item") == 0) {
        return start_item(map, doc, event);
    }
    if (strcmp(event->name, "name") == 0 || strcmp(event->name, "price") == 0) {
        int is_name = strcmp(event->name, "name") == 0;
        int required = is_name ? NEED_NAME : NEED_PRICE;
        if (map->state != required) {
            return reject(doc, is_name ? "name order or nesting" : "price order or nesting");
        }
        if (check_attributes(doc, event, NULL) != 0) {
            return -1;
        }
        map->state = is_name ? IN_NAME : IN_PRICE;
        map->value_started = 0;
        return 0;
    }
    char rule[96];
    snprintf(rule, sizeof rule, "unknown element %.63s", event->name);
    return reject(doc, rule);
}

static int field_text(CatalogMap *map, KiDoc *doc, const KiEvent *event) {
    if (map->state != IN_NAME && map->state != IN_PRICE) {
        if (only_space(event->text, event->text_len)) {
            return 0;
        }
        return reject(doc, "structural text");
    }
    if (event->text_len == 0) {
        return 0;
    }
    if (!map->value_started) {
        const char *label = map->state == IN_NAME ? "Name: " : "Price: ";
        if (ki_doc_text(doc, label, strlen(label)) != 0) {
            return -1;
        }
        map->value_started = 1;
    }
    /* TEXT and CDATA follow the same path. UTF-8 may span fragments. */
    return ki_doc_text(doc, event->text, event->text_len);
}

static int finish_field(CatalogMap *map, KiDoc *doc, int is_name) {
    if (map->state != (is_name ? IN_NAME : IN_PRICE)) {
        return reject(doc, is_name ? "name nesting" : "price nesting");
    }
    if (!map->value_started) {
        const char *label = is_name ? "Name:" : "Price:";
        if (ki_doc_text(doc, label, strlen(label)) != 0) {
            return -1;
        }
    }
    if (ki_doc_break(doc) != 0) {
        return -1;
    }
    map->state = is_name ? NEED_PRICE : ITEM_DONE;
    return 0;
}

static int end_element(CatalogMap *map, KiDoc *doc, const KiEvent *event) {
    if (strcmp(event->name, "name") == 0) {
        return finish_field(map, doc, 1);
    }
    if (strcmp(event->name, "price") == 0) {
        return finish_field(map, doc, 0);
    }
    if (strcmp(event->name, "item") == 0) {
        if (map->state != ITEM_DONE) {
            return reject(doc, "item requires name then price");
        }
        map->state = IN_CATALOG;
        return 0;
    }
    if (strcmp(event->name, "catalog") == 0) {
        if (map->state != IN_CATALOG) {
            return reject(doc, "catalog nesting");
        }
        char total[48];
        snprintf(total, sizeof total, "Items: %llu", map->items);
        if (emit_line(doc, total) != 0) {
            return -1;
        }
        map->state = AFTER_CATALOG;
        return 0;
    }
    return reject(doc, "unknown end element");
}

int catalog_map_event(void *user, KiDoc *doc, const KiEvent *event) {
    CatalogMap *map = user;
    if (event->kind == KI_EV_START) {
        return start_element(map, doc, event);
    }
    if (event->kind == KI_EV_TEXT) {
        return field_text(map, doc, event);
    }
    if (event->kind == KI_EV_END) {
        return end_element(map, doc, event);
    }
    return reject(doc, "unknown event");
}

