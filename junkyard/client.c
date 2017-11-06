/*
* Copyright (c) 2017 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <dst/dst.h>
#include "bootstrap.h"

void teststr(Dst *vm, const char *src) {
    uint32_t len = 0;
    const uint8_t *bytes;
    const char *ns = NULL;
    int status = dst_parsec(vm, src, &ns);
    if (status) {
        printf("Parse failed: ");
        bytes = dst_bytes(vm, -1, &len);
        for (uint32_t i = 0; i < len; i++)
            putc(bytes[i], stdout);
        putc('\n', stdout);
        printf("%s\n", src);
        for (const char *scan = src + 1; scan < ns; ++scan)
            putc(' ', stdout);
        putc('^', stdout);
        putc('\n', stdout);
        return;
    }
    dst_description(vm);
    bytes = dst_bytes(vm, -1, &len);
    for (uint32_t i = 0; i < len; i++)
        putc(bytes[i], stdout);
    putc('\n', stdout);
}

int main() {

    Dst *vm = dst_init();

    teststr(vm, "[+ 1 2 3 \"porkpie\" ]");
    teststr(vm, "(+ 1 2 \t asdajs 1035.89 3)");
    teststr(vm, "[+ 1 2 :bokr]");
    teststr(vm, "{+ 1 2 3}");

    dst_deinit(vm);

    return 0;
}
