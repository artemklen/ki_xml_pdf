#define _POSIX_C_SOURCE 200809L
#include <ki_xml_pdf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    FILE *in, *out;
    const char *fault;
    long reads, input_bytes, writes, accepted;
    int eof, shorted, fault_hits, refused_bytes;
    unsigned long guard;
} Io;
static int read_cb(void *user, char *dst, int max) {
    Io *io = (Io *)user;
    io->reads++;
    if (!strcmp(io->fault, "negative-read") && io->input_bytes >= 800) return -1;
    size_t n = fread(dst, 1, (size_t)(max < 8 ? max : 8), io->in);
    io->input_bytes += (long)n;
    if (!n) io->eof = 1;
    return ferror(io->in) ? -1 : (int)n;
}
static int write_cb(void *user, const char *data, int n) {
    Io *io = (Io *)user;
    io->writes++;
    if (io->writes == 2 && !strcmp(io->fault, "negative-write")) {
        io->fault_hits++;
        io->refused_bytes += n;
        fprintf(stderr, "write call 2: requested=%d returned=-1 accepted=0\n", n);
        return -1;
    }
    if (io->writes == 2 && !strcmp(io->fault, "short-write")) {
        int requested = n;
        n /= 2;
        io->shorted = 1;
        io->fault_hits++;
        io->refused_bytes += requested - n;
        fprintf(stderr, "write call 2: requested=%d short accepted=%d\n", requested, n);
    }
    int got = (int)fwrite(data, 1, (size_t)n, io->out);
    io->accepted += got;
    return got;
}
static int cancel_cb(void *user) {
    Io *io = (Io *)user;
    return !strcmp(io->fault, "cancel-at-eof") && io->eof;
}
static int call(const char *route, const char *in, const char *out, KiOptions *o, KiConfig *c, KiResult *r) {
    if (!strcmp(route, "file")) return ki_convert_file(in, out, o, r);
    if (!strcmp(route, "callback")) return ki_convert(o, r);
    if (!strcmp(route, "tofile")) return ki_convert_to_file(out, o, r);
    if (!strcmp(route, "file_cfg")) return ki_convert_file_cfg(in, out, o, c, r);
    if (!strcmp(route, "callback_cfg")) return ki_convert_cfg(o, c, r);
    return ki_convert_to_file_cfg(out, o, c, r);
}
int main(int argc, char **argv) {
    if (argc != 8) return 99;
    const char *route = argv[1], *fault = argv[2], *input = argv[3], *dest = argv[4];
    Io io;
    memset(&io, 0, sizeof io);
    io.fault = fault;
    io.in = fopen(input, "rb");
    io.out = tmpfile();
    io.guard = 0x1234abcdUL;
    if (!io.in || !io.out) return 98;
    int fi = fileno(io.in), fo = fileno(io.out);
    KiOptions o;
    ki_options_init(&o);
    o.read_bytes = read_cb;
    o.write_bytes = write_cb;
    o.io_user = &io;
    o.cancelled = cancel_cb;
    o.cancel_user = &io;
    o.temp_dir = argv[6];
    if (!strcmp(fault, "font")) o.font_path = argv[5];
    if (!strcmp(fault, "profile")) o.profile = KI_PROFILE_REPORT;
    if (!strcmp(fault, "page-quota")) o.max_pages = 1;
    if (!strcmp(fault, "output-quota")) o.max_output_bytes = 4096;
    if (!strcmp(fault, "spool-quota")) o.max_spool_bytes = 8;
    if (!strcmp(fault, "invalid-option")) o.max_pages = -1;
    char absent[1024];
    snprintf(absent, sizeof absent, "%s.absent", argv[7]);
    if (!strcmp(fault, "input-open")) input = absent;
    if (!strcmp(fault, "input-read")) input = argv[6]; /* directory */
    KiConfig cfg;
    ki_config_init(&cfg, sizeof cfg);
    if (!strcmp(fault, "invalid-geometry")) cfg.margin_left = -1;
    if (!strcmp(fault, "invalid-metadata")) cfg.creation_date = "not-a-date";
    KiResult result;
    memset(&result, 0x5a, sizeof result);
    int rc = call(route, input, dest, &o, &cfg, &result);
    KiResult first = result;
    fprintf(stderr, "%s: status=%d cause=%d message=%s\n", fault, rc, first.cause, first.message);
    int input_open = fcntl(fi, F_GETFD) != -1, output_open = fcntl(fo, F_GETFD) != -1;
    int reusable = input_open && output_open && !fseek(io.in, 0, SEEK_SET) && fgetc(io.in) == '<';
    long reads = io.reads, writes = io.writes, accepted = io.accepted, input_bytes = io.input_bytes;
    int guard = io.guard == 0x1234abcdUL;
    int fault_hits = io.fault_hits, refused = io.refused_bytes;
    if (strstr(route, "callback") == route) {
        char path[1024];
        snprintf(path, sizeof path, "%s.callback.pdf", dest);
        FILE *captured = fopen(path, "wb");
        if (!captured || fflush(io.out) || fseek(io.out, 0, SEEK_SET)) return 95;
        int c;
        while ((c = fgetc(io.out)) != EOF) {
            if (fputc(c, captured) == EOF) return 95;
        }
        if (fclose(captured)) return 95;
    }
    /* Reuse caller-owned streams/options/result after the failed call. */
    rewind(io.in);
    io.fault = "";
    io.reads = io.writes = io.accepted = io.input_bytes = 0;
    io.eof = 0;
    o.font_path = NULL;
    o.profile = KI_PROFILE_RECORDS;
    o.max_pages = 0;
    o.max_output_bytes = o.max_spool_bytes = 0;
    /* Malformed-input failure uses a fresh valid fixture for the retry. */
    fclose(io.in);
    io.in = fopen(argv[7], "rb");
    if (!io.in) return 97;
    int retry = ki_convert(&o, &result);
    printf("{\"status\":%d,\"result_status\":%d,\"cause\":%d,\"pages\":%d,\"objects\":%d,"
           "\"output_bytes\":%ld,\"accepted_bytes\":%ld,\"read_calls\":%ld,\"input_bytes\":%ld,"
           "\"write_calls\":%ld,\"input_open\":%d,\"output_open\":%d,\"caller_reusable\":%d,"
           "\"guard_ok\":%d,\"fault_hits\":%d,\"refused_bytes\":%d,\"retry_status\":%d,\"retry_cause\":%d,\"retry_output_bytes\":%ld}\n",
           rc, first.status, first.cause, first.pages, first.objects, first.output_bytes, accepted,
           reads, input_bytes, writes, input_open, output_open, reusable, guard, fault_hits, refused,
           retry, result.cause, result.output_bytes);
    int close_status = fclose(io.in) || fclose(io.out);
    return close_status ? 96 : 0;
}
