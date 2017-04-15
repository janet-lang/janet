#include <gst/gst.h>

/* Compares two strings */
int gst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = gst_string_length(lhs);
    uint32_t ylen = gst_string_length(rhs);
    uint32_t len = xlen > ylen ? ylen : xlen;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less than y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}
