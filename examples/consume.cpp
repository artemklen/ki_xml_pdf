#include "ki_xml_pdf.h"

#include <cstdio>

int main(int argc, char **argv) {
    const char *in = argc > 1 ? argv[1] : "examples/hello.xml";
    const char *out = argc > 2 ? argv[2] : "hello-cpp.pdf";
    KiResult result;
    int rc = ki_convert_file(in, out, NULL, &result);
    if (rc != 0) {
        std::fprintf(stderr, "consume: %s\n", result.message);
        return rc;
    }
    std::printf("pages=%d\n", result.pages);
    return 0;
}
