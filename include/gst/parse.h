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
		GST_PARSER_ERROR
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

#endif /* end of include guard: PARSE_H_ONYWMADW */
