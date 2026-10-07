#include "report.h"

#include <stdio.h>
#include <string.h>

void report_init(Report *r, long offset, long limit) {
    memset(r, 0, sizeof *r);
    r->offset = offset > 0 ? (unsigned long long)offset : 0;
    r->limit = limit > 0 ? (unsigned long long)limit : 0;
}

static int only_space(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return 0;
        }
    }
    return 1;
}

static int has_graph(const char *s) {
    return s != NULL && !only_space(s, strlen(s));
}

static const char *attr_get(const KiEvent *ev, const char *name) {
    for (int i = 0; i < ev->nattr; i++) {
        if (strcmp(ev->attr_name[i], name) == 0) {
            return ev->attr_val[i];
        }
    }
    return NULL;
}

static int bad_attr(const KiEvent *ev, int allow_space) {
    for (int i = 0; i < ev->nattr; i++) {
        const char *name = ev->attr_name[i];
        if (strchr(name, ':') != NULL) {
            if (!(allow_space && strcmp(name, "xml:space") == 0)) {
                return 1;
            }
        }
        if (strncmp(name, "xmlns", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

static int fail_rule(Report *r, KiDoc *doc, const char *rule) {
    char buf[160];
    snprintf(buf, sizeof buf, "%s at byte %zu", rule, r->at);
    return ki_doc_fail(doc, buf);
}

static int fail_attr(Report *r, KiDoc *doc, const char *element, const char *attr) {
    char buf[160];
    snprintf(buf, sizeof buf, "profile: unknown attribute %s on %s at byte %zu", attr, element, r->at);
    return ki_doc_fail(doc, buf);
}

static int emit(KiDoc *doc, const char *s) {
    if (ki_doc_text(doc, s, strlen(s)) != 0) {
        return -1;
    }
    return ki_doc_break(doc);
}

static int start_report(Report *r, KiDoc *doc, const KiEvent *ev) {
    if (r->in_report || r->in_record || r->in_field) {
        return fail_rule(r, doc, "profile: report nesting");
    }
    if (bad_attr(ev, 0) || strchr(ev->name, ':') != NULL) {
        return fail_rule(r, doc, "profile: namespace");
    }
    const char *version = attr_get(ev, "version");
    const char *title = attr_get(ev, "title");
    if (version == NULL || strcmp(version, "1") != 0) {
        return fail_rule(r, doc, "profile: report version");
    }
    if (!has_graph(title)) {
        return fail_rule(r, doc, "profile: report title");
    }
    for (int i = 0; i < ev->nattr; i++) {
        if (strcmp(ev->attr_name[i], "version") != 0 && strcmp(ev->attr_name[i], "title") != 0) {
            return fail_attr(r, doc, ev->name, ev->attr_name[i]);
        }
    }
    if (emit(doc, title) != 0) {
        return -1;
    }
    r->in_report = 1;
    return 0;
}

static int start_record(Report *r, KiDoc *doc, const KiEvent *ev) {
    if (!r->in_report || r->in_record || r->in_field) {
        return fail_rule(r, doc, "profile: record nesting");
    }
    if (bad_attr(ev, 0)) {
        return fail_rule(r, doc, "profile: namespace");
    }
    const char *id = attr_get(ev, "id");
    if (id != NULL && !has_graph(id)) {
        return fail_rule(r, doc, "profile: empty id");
    }
    for (int i = 0; i < ev->nattr; i++) {
        if (strcmp(ev->attr_name[i], "id") != 0) {
            return fail_attr(r, doc, ev->name, ev->attr_name[i]);
        }
    }
    if (r->total == ~0ull) {
        return fail_rule(r, doc, "profile: record counter");
    }
    r->total++;
    r->index++;
    int take = r->index > r->offset && (r->limit == 0 || r->selected < r->limit);
    r->skipping = !take;
    r->saw_field = 0;
    r->in_record = 1;
    if (!take) {
        return 0;
    }
    if (r->selected == ~0ull) {
        return fail_rule(r, doc, "profile: record counter");
    }
    r->selected++;
    char line[320];
    if (id != NULL) {
        snprintf(line, sizeof line, "Запись %s", id);
    } else {
        snprintf(line, sizeof line, "Запись %llu", r->index);
    }
    return emit(doc, line);
}

static int start_field(Report *r, KiDoc *doc, const KiEvent *ev) {
    if (!r->in_record || r->in_field) {
        return fail_rule(r, doc, "profile: field nesting");
    }
    if (bad_attr(ev, 1)) {
        return fail_rule(r, doc, "profile: namespace");
    }
    const char *name = attr_get(ev, "name");
    if (!has_graph(name)) {
        return fail_rule(r, doc, "profile: field name");
    }
    const char *space = attr_get(ev, "xml:space");
    if (space != NULL && strcmp(space, "default") != 0 && strcmp(space, "preserve") != 0) {
        return fail_rule(r, doc, "profile: xml:space");
    }
    for (int i = 0; i < ev->nattr; i++) {
        if (strcmp(ev->attr_name[i], "name") != 0 && strcmp(ev->attr_name[i], "xml:space") != 0) {
            return fail_attr(r, doc, ev->name, ev->attr_name[i]);
        }
    }
    snprintf(r->pending_name, sizeof r->pending_name, "%s", name);
    r->value_started = 0;
    r->saw_field = 1;
    r->in_field = 1;
    return 0;
}

static int on_text(Report *r, KiDoc *doc, const KiEvent *ev) {
    if (!r->in_field) {
        if (ev->text_len == 0 || only_space(ev->text, ev->text_len)) {
            return 0;
        }
        return fail_rule(r, doc, "profile: structural text");
    }
    if (r->skipping || ev->text_len == 0) {
        return 0;
    }
    if (!r->value_started) {
        if (ki_doc_text(doc, r->pending_name, strlen(r->pending_name)) != 0 || ki_doc_text(doc, ": ", 2) != 0) {
            return -1;
        }
        r->value_started = 1;
    }
    return ki_doc_text(doc, ev->text, ev->text_len);
}

static int end_field(Report *r, KiDoc *doc) {
    if (!r->in_field) {
        return fail_rule(r, doc, "profile: field nesting");
    }
    r->in_field = 0;
    if (r->skipping) {
        return 0;
    }
    if (!r->value_started) {
        if (ki_doc_text(doc, r->pending_name, strlen(r->pending_name)) != 0 || ki_doc_text(doc, ":", 1) != 0) {
            return -1;
        }
    }
    return ki_doc_break(doc);
}

static int end_record(Report *r, KiDoc *doc) {
    if (!r->in_record || r->in_field) {
        return fail_rule(r, doc, "profile: record nesting");
    }
    if (!r->saw_field) {
        return fail_rule(r, doc, "profile: empty record");
    }
    r->in_record = 0;
    r->skipping = 0;
    return 0;
}

static int end_report(Report *r, KiDoc *doc) {
    if (!r->in_report || r->in_record || r->in_field) {
        return fail_rule(r, doc, "profile: report nesting");
    }
    char line[80];
    unsigned long long skipped = r->total - r->selected;
    snprintf(line, sizeof line, "Всего записей: %llu", r->total);
    if (emit(doc, line) != 0) {
        return -1;
    }
    snprintf(line, sizeof line, "Выбрано записей: %llu", r->selected);
    if (emit(doc, line) != 0) {
        return -1;
    }
    snprintf(line, sizeof line, "Пропущено записей: %llu", skipped);
    if (emit(doc, line) != 0) {
        return -1;
    }
    r->in_report = 0;
    return 0;
}

int report_map(Report *r, KiDoc *doc, const KiEvent *ev) {
    r->at = ev->byte_offset;
    if (ev->kind == KI_EV_START) {
        if (strcmp(ev->name, "report") == 0) {
            return start_report(r, doc, ev);
        }
        if (strcmp(ev->name, "record") == 0) {
            return start_record(r, doc, ev);
        }
        if (strcmp(ev->name, "field") == 0) {
            return start_field(r, doc, ev);
        }
        return fail_rule(r, doc, "profile: unknown element");
    }
    if (ev->kind == KI_EV_END) {
        if (strcmp(ev->name, "field") == 0) {
            return end_field(r, doc);
        }
        if (strcmp(ev->name, "record") == 0) {
            return end_record(r, doc);
        }
        if (strcmp(ev->name, "report") == 0) {
            return end_report(r, doc);
        }
        return fail_rule(r, doc, "profile: unknown element");
    }
    if (ev->kind == KI_EV_TEXT) {
        return on_text(r, doc, ev);
    }
    return 0;
}
