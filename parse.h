#ifndef PARSE_H_ONYWMADW
#define PARSE_H_ONYWMADW

#include "datatypes.h"

/* Initialize a parser */
void gst_parser(GstParser *p, Gst *vm);

/* Parse a c style string. Returns true if successful */
int gst_parse_cstring(GstParser *p, const char *string);

#endif /* end of include guard: PARSE_H_ONYWMADW */
