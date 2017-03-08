#ifndef PARSE_H_ONYWMADW
#define PARSE_H_ONYWMADW

#include "datatypes.h"

/* Some parser flags */
#define GST_PARSER_FLAG_INCOMMENT 1
#define GST_PARSER_FLAG_EXPECTING_COMMENT 2

/* Initialize a parser */
void gst_parser(GstParser *p, Gst *vm);

/* Parse a c style string. Returns true if successful */
int gst_parse_cstring(GstParser *p, const char *string);

#endif /* end of include guard: PARSE_H_ONYWMADW */
