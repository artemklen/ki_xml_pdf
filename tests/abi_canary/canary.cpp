// SPDX-License-Identifier: MIT
// C++ ABI canary for the 0.2 public API; see canary.c for the procedure.
// usage: abi_canary_cpp OUTDIR [FONT]
#include "ki_xml_pdf.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void expect(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "canary FAIL %s\n", what);
        failures++;
    }
}

struct MemIn {
    std::string data;
    std::size_t pos;
};

extern "C" int mem_read(void *user, char *dst, int max) {
    MemIn *in = static_cast<MemIn *>(user);
    if (max <= 0) {
        return -1;
    }
    std::size_t left = in->data.size() - in->pos;
    if (left == 0) {
        return 0;
    }
    // Short reads of up to 3 bytes.
    std::size_t n = left < 3 ? left : 3;
    if (n > static_cast<std::size_t>(max)) {
        n = static_cast<std::size_t>(max);
    }
    std::memcpy(dst, in->data.data() + in->pos, n);
    in->pos += n;
    return static_cast<int>(n);
}

struct CbIo {
    MemIn in;
    std::FILE *out;
};

extern "C" int cb_read(void *user, char *dst, int max) {
    return mem_read(&static_cast<CbIo *>(user)->in, dst, max);
}

extern "C" int cb_write(void *user, const char *src, int n) {
    CbIo *io = static_cast<CbIo *>(user);
    return std::fwrite(src, 1, static_cast<std::size_t>(n), io->out) == static_cast<std::size_t>(n)
               ? n
               : -1;
}

int mapper_calls = 0;

extern "C" int canary_map(void *, KiDoc *doc, const KiEvent *ev) {
    mapper_calls++;
    if (ev->kind == KI_EV_TEXT && ev->text_len > 0) {
        static const char prefix[] = "CPP_MAPPED:";
        if (ki_doc_text(doc, prefix, sizeof prefix - 1) != 0 ||
            ki_doc_text(doc, ev->text, ev->text_len) != 0 || ki_doc_break(doc) != 0) {
            return -1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s OUTDIR [FONT]\n", argv[0]);
        return 2;
    }
    std::string dir = argv[1];
    const char *font = argc > 2 ? argv[2] : nullptr;
    std::printf("layout KiOptions size=%zu read_bytes=%zu io_user=%zu\n", sizeof(KiOptions),
                offsetof(KiOptions, read_bytes), offsetof(KiOptions, io_user));
    std::printf("layout KiResult size=%zu report_skipped=%zu\n", sizeof(KiResult),
                offsetof(KiResult, report_skipped));
    std::printf("layout KiEvent size=%zu byte_offset=%zu\n", sizeof(KiEvent),
                offsetof(KiEvent, byte_offset));

    KiOptions opt;
    KiResult res;

    std::string in_path = dir + "/cpp-file.xml";
    std::string pdf_path = dir + "/cpp-file.pdf";
    {
        std::FILE *fp = std::fopen(in_path.c_str(), "wb");
        expect(fp != nullptr, "open input");
        if (fp != nullptr) {
            std::fputs("<doc><item>ABI_CPP_FILE_MARKER</item></doc>", fp);
            expect(std::fclose(fp) == 0, "close input");
        }
    }
    ki_options_init(&opt);
    expect(opt.size == sizeof(KiOptions), "ki_options_init size");
    opt.font_path = font;
    int rc = ki_convert_file(in_path.c_str(), pdf_path.c_str(), &opt, &res);
    expect(rc == 0 && res.pages == 1, "ki_convert_file");
    std::printf("entry ki_convert_file rc=%d pages=%d\n", rc, res.pages);

    CbIo io;
    io.in.data =
        "<report version=\"1\" title=\"ABI_CPP_REPORT\">"
        "<record><field name=\"v\">ABI_CPP_REC_1</field></record>"
        "<record><field name=\"v\">ABI_CPP_REC_2</field></record>"
        "</report>";
    io.in.pos = 0;
    pdf_path = dir + "/cpp-callback.pdf";
    io.out = std::fopen(pdf_path.c_str(), "wb");
    expect(io.out != nullptr, "open callback output");
    ki_options_init(&opt);
    opt.font_path = font;
    opt.profile = KI_PROFILE_REPORT;
    opt.record_offset = 1;
    opt.read_bytes = cb_read;
    opt.write_bytes = cb_write;
    opt.io_user = &io;
    rc = io.out != nullptr ? ki_convert(&opt, &res) : -1;
    if (io.out != nullptr) {
        expect(std::fclose(io.out) == 0, "close callback output");
    }
    expect(rc == 0 && res.report_total == 2 && res.report_selected == 1 && res.report_skipped == 1,
           "ki_convert report counters");
    std::printf("entry ki_convert rc=%d total=%llu selected=%llu skipped=%llu\n", rc,
                res.report_total, res.report_selected, res.report_skipped);

    MemIn min;
    min.data = "<doc><x>ABI_CPP_MAPPED</x></doc>";
    min.pos = 0;
    pdf_path = dir + "/cpp-mapper.pdf";
    ki_options_init(&opt);
    opt.font_path = font;
    opt.map = canary_map;
    opt.read_bytes = mem_read;
    opt.io_user = &min;
    rc = ki_convert_to_file(pdf_path.c_str(), &opt, &res);
    expect(rc == 0 && mapper_calls > 0, "ki_convert_to_file custom mapper");
    std::printf("entry ki_convert_to_file rc=%d mapper_calls=%d\n", rc, mapper_calls);

    if (failures != 0) {
        std::printf("canary cpp failures=%d\n", failures);
        return 1;
    }
    std::printf("canary cpp ok\n");
    return 0;
}
