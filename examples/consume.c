#include "ki_xml_pdf.h"

#include <stdio.h>

/* Build from another directory:
     cc -I$PREFIX/include examples/consume.c -L$PREFIX/lib -lki_xml_pdf -o consume
   The default font uses the library's build PREFIX. A relocated installation
   can supply KiOptions.font_path explicitly. */

int main(int argc, char **argv) {
    const char *in = argc > 1 ? argv[1] : "examples/hello.xml";
    const char *out = argc > 2 ? argv[2] : "hello.pdf";
    KiResult result;
    int rc = ki_convert_file(in, out, NULL, &result);
    if (rc != 0) {
        fprintf(stderr, "consume: %s\n", result.message);
        return rc;
    }
    printf("pages=%d\n", result.pages);
    return 0;
}
