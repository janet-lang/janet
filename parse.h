#ifndef PARSE_H_ONYWMADW
#define PARSE_H_ONYWMADW

#include "datatypes.h"

#define PARSE_ERROR -1
#define PARSE_VALUE_READ 1
#define PARSE_VALUE_PENDING 0

void ParserInit(Parser * p, VM * vm);

int ParserParseCString(Parser * p, const char * string);

#endif /* end of include guard: PARSE_H_ONYWMADW */
