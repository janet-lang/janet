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

#ifndef PARSE_H_ONYWMADW
#define PARSE_H_ONYWMADW

#include <gst/gst.h>

typedef struct GstParser GstParser;
typedef struct GstParseState GstParseState;

/* Holds the parsing state */
struct GstParser {
    Gst *vm;
    const char *error;
    GstParseState *data;
    GstValue value;
    uint32_t count;
    uint32_t cap;
    uint32_t index;
    uint32_t flags;
    uint32_t quoteCount;
    enum {
		GST_PARSER_PENDING = 0,
		GST_PARSER_FULL,
		GST_PARSER_ERROR,
        GST_PARSER_ROOT
    } status;
};

/* Some parser flags */
#define GST_PARSER_FLAG_INCOMMENT 1
#define GST_PARSER_FLAG_EXPECTING_COMMENT 2

/* Initialize a parser */
void gst_parser(GstParser *p, Gst *vm);

/* Parse a c style string. Returns number of bytes read */
int gst_parse_cstring(GstParser *p, const char *string);

/* Parse a gst string. Returns number of bytes read */
int gst_parse_string(GstParser *p, const uint8_t *string);

/* Check if a parser has a value that needs to be handled. If
 * so, the parser will not parse any more input until that value
 * is consumed. */
int gst_parse_hasvalue(GstParser *p);

/* Gets a value from the parser */
GstValue gst_parse_consume(GstParser *p);

#endif /* end of include guard: PARSE_H_ONYWMADW */
