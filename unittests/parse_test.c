#include "unit.h"
#include <dst/dst.h>

int main() {
    DstParseResult pres;
    const uint8_t *str;

    dst_init();

    pres = dst_parsec("'(+ 1 () [] 3 5 :hello \"hi\\h41\")");

    assert(pres.status == DST_PARSE_OK);
    assert(pres.result.value.type == DST_TUPLE);

    str = dst_to_string(pres.result.value);
    printf("%.*s\n", dst_string_length(str), (const char *) str);

    return 0;
}
