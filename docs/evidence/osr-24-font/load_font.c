#include "font.h"
#include <stdio.h>
int main(int argc, char **argv) {
    FontFace face;
    int rc = font_load(&face, argv[1]);
    printf("font_load rc=%d\n", rc);
    if (rc == 0) font_free(&face);
    return rc == 0 ? 0 : 3;
}
