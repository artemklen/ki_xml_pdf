/* SPDX-License-Identifier: MIT */
#include "catalog_map.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *input;
    int chunk_bytes;
} CatalogReader;

static int usage(void) {
    fprintf(stderr, "catalog: cause=%d usage: catalog [--max-pages N] INPUT OUTPUT [CHUNK_BYTES]\n",
            KI_CAUSE_USAGE);
    return 2;
}

static int positive_int(const char *text, int *value) {
    if (*text == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }
    errno = 0;
    char *end;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int read_catalog(void *user, char *destination, int maximum) {
    CatalogReader *reader = user;
    if (maximum > reader->chunk_bytes) {
        maximum = reader->chunk_bytes;
    }
    size_t count = fread(destination, 1, (size_t)maximum, reader->input);
    if (count == 0 && ferror(reader->input)) {
        return -1;
    }
    return (int)count;
}

int main(int argc, char **argv) {
    int first = 1;
    int max_pages = 0;
    if (argc > first && strcmp(argv[first], "--max-pages") == 0) {
        if (argc <= first + 1 || positive_int(argv[first + 1], &max_pages) != 0) {
            return usage();
        }
        first += 2;
    }
    int remaining = argc - first;
    if (remaining != 2 && remaining != 3) {
        return usage();
    }
    int chunk_bytes = 0;
    if (remaining == 3 && positive_int(argv[first + 2], &chunk_bytes) != 0) {
        return usage();
    }
    const char *input = argv[first];
    const char *output = argv[first + 1];
    CatalogMap mapper;
    catalog_map_init(&mapper);
    KiOptions options;
    ki_options_init(&options);
    options.map = catalog_map_event;
    options.map_user = &mapper;
    options.max_pages = max_pages;
    KiResult result;
    int status;
    if (chunk_bytes == 0) {
        status = ki_convert_file(input, output, &options, &result);
    } else {
        CatalogReader reader;
        reader.input = fopen(input, "rb");
        reader.chunk_bytes = chunk_bytes;
        if (reader.input == NULL) {
            fprintf(stderr, "catalog: cause=%d cannot open input: %s\n", KI_CAUSE_IO, input);
            return 3;
        }
        options.read_bytes = read_catalog;
        options.io_user = &reader;
        status = ki_convert_to_file(output, &options, &result);
        /* The application owns this FILE; the library does not close it. */
        fclose(reader.input);
    }
    if (status != 0) {
        fprintf(stderr, "catalog: cause=%d %s\n", result.cause, result.message);
        return status;
    }
    printf("items=%llu pages=%d output_bytes=%ld\n",
           mapper.items, result.pages, result.output_bytes);
    return 0;
}

