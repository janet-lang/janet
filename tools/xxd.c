/*
* Copyright (c) 2019 Calvin Rose
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

/* Simple clone of the xxd tool used at build time. Used to
 * create headers out of source files. Only used for core libraries
 * like the bootstrapping code and the stl. */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define BUFSIZE 1024
#define PERLINE 10

int main(int argc, const char **argv) {

    static const char hex[] = "0123456789ABCDEF";
    char buf[BUFSIZE];
    size_t bytesRead = 0;
    int32_t totalRead = 0;
    int lineIndex = 0;
    int line = 0;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s infile outfile symbol\n", argv[0]);
        return 1;
    }

    /* Open the files */
    FILE *in = fopen(argv[1], "rb");
    FILE *out = fopen(argv[2], "wb");

    /* Check if files open successfully */
    if (in == NULL) {
        fprintf(stderr, "Could not open input file %s\n", argv[1]);
        return 1;
    } else if (out == NULL) {
        fprintf(stderr, "Could not open output file %s\n", argv[2]);
        return 1;
    }

    /* Write the header */
    fprintf(out, "/* Auto generated - DO NOT EDIT */\n\n#include <stdint.h>\n\n");
    fprintf(out, "static const unsigned char bytes_%s[] = {", argv[3]);

    /* Read in chunks from buffer */
    while ((bytesRead = fread(buf, 1, sizeof(buf), in)) > 0) {
        size_t i;
        totalRead += bytesRead;
        for (i = 0; i < bytesRead; ++i) {
            int byte = ((uint8_t *)buf) [i];

            /* Write the byte */
            if (lineIndex++ == 0) {
                if (line++)
                    fputc(',', out);
                fputs("\n    ", out);
            } else {
                fputs(", ", out);
            }
            fputs("0x", out);
            fputc(hex[byte >> 4], out);
            fputc(hex[byte & 0xF], out);

            /* Make line index wrap */
            if (lineIndex >= PERLINE)
                lineIndex = 0;

        }
    }

    /* Write the tail */
    fputs("\n};\n\n", out);

    fprintf(out, "const unsigned char *%s = bytes_%s;\n\n", argv[3], argv[3]);

    /* Write chunk size */
    fprintf(out, "int32_t %s_size = %d;\n", argv[3], totalRead);

    /* Close the file handles */
    fclose(in);
    fclose(out);

    return 0;
}
