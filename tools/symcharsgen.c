/*
* Copyright (c) 2024 Calvin Rose
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

#include <stdio.h>
#include <stdint.h>

static int is_symbol_char_gen(uint8_t c) {
    if (c & 0x80) return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return (c == '!' ||
            c == '$' ||
            c == '%' ||
            c == '&' ||
            c == '*' ||
            c == '+' ||
            c == '-' ||
            c == '.' ||
            c == '/' ||
            c == ':' ||
            c == '<' ||
            c == '?' ||
            c == '=' ||
            c == '>' ||
            c == '@' ||
            c == '^' ||
            c == '_');
}

int main() {
    printf("static const uint32_t symchars[8] = {\n    ");
    for (int i = 0; i < 256; i += 32) {
        uint32_t block = 0;
        for (int j = 0; j < 32; j++) {
            block |= is_symbol_char_gen(i + j) << j;
        }
        printf("0x%08x%s", block, (i == (256 - 32)) ? "" : ", ");
    }
    printf("\n};\n");
    return 0;
}

