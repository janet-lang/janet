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

/* Use a custom double parser instead of libc's strtod for better portability
 * and control.
 *
 * This version has been modified for much greater flexibility in parsing, such
 * as choosing the radix and supporting scientific notation with any radix.
 *
 * Numbers are of the form [-+]R[rR]I.F[eE&][-+]X in pseudo-regex form, where R
 * is the radix, I is the integer part, F is the fractional part, and X is the
 * exponent. All signs, radix, decimal point, fractional part, and exponent can
 * be omitted.  The radix is assumed to be 10 if omitted, and the E or e
 * separator for the exponent can only be used when the radix is 10. This is
 * because E is a valid digit in bases 15 or greater. For bases greater than
 * 10, the letters are used as digits. A through Z correspond to the digits 10
 * through 35, and the lowercase letters have the same values. The radix number
 * is always in base 10. For example, a hexadecimal number could be written
 * '16rdeadbeef'. janet_scan_number also supports some c style syntax for
 * hexadecimal literals. The previous number could also be written
 * '0xdeadbeef'.
 */

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#endif

#include <math.h>
#include <string.h>

/* Lookup table for getting values of characters when parsing numbers. Handles
 * digits 0-9 and a-z (and A-Z). A-Z have values of 10 to 35. */
static uint8_t digit_lookup[128] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define BIGNAT_NBIT 31
#define BIGNAT_BASE 0x80000000U

/* Allow for large mantissa. BigNat is a natural number. */
struct BigNat {
    uint32_t first_digit; /* First digit so we don't need to allocate when not needed. */
    int32_t n; /* n digits */
    int32_t cap; /* allocated digit capacity */
    uint32_t *digits; /* Each digit is base (2 ^ 31). Digits are least significant first. */
};

/* Initialize a bignat to 0 */
static void bignat_zero(struct BigNat *x) {
    x->first_digit = 0;
    x->n = 0;
    x->cap = 0;
    x->digits = NULL;
}

/* Allocate n more digits for mant. Return a pointer to these digits. */
static uint32_t *bignat_extra(struct BigNat *mant, int32_t n) {
    int32_t oldn = mant->n;
    int32_t newn = oldn + n;
    if (mant->cap < newn) {
        int32_t newcap = 2 * newn;
        uint32_t *mem = janet_realloc(mant->digits, (size_t) newcap * sizeof(uint32_t));
        if (NULL == mem) {
            JANET_OUT_OF_MEMORY;
        }
        mant->cap = newcap;
        mant->digits = mem;
    }
    mant->n = newn;
    return mant->digits + oldn;
}

/* Append a digit */
static void bignat_append(struct BigNat *mant, uint32_t dig) {
    bignat_extra(mant, 1)[0] = dig;
}

/* Multiply the mantissa mant by a factor and the add a term
 * in one operation. factor will be between 2 and 36^4,
 * term will be between 0 and 36. */
static void bignat_muladd(struct BigNat *mant, uint32_t factor, uint32_t term) {
    int32_t i;
    uint64_t carry = ((uint64_t) mant->first_digit) * factor + term;
    mant->first_digit = carry % BIGNAT_BASE;
    carry /= BIGNAT_BASE;
    for (i = 0; i < mant->n; i++) {
        carry += ((uint64_t) mant->digits[i]) * factor;
        mant->digits[i] = carry % BIGNAT_BASE;
        carry /= BIGNAT_BASE;
    }
    if (carry) bignat_append(mant, (uint32_t) carry);
}

/* Divide the mantissa mant by a factor. Drop the remainder. */
static void bignat_div(struct BigNat *mant, uint32_t divisor) {
    int32_t i;
    uint32_t quotient, remainder;
    uint64_t dividend;
    remainder = 0, quotient = 0;
    for (i = mant->n - 1; i >= 0; i--) {
        dividend = ((uint64_t)remainder * BIGNAT_BASE) + mant->digits[i];
        if (i < mant->n - 1) mant->digits[i + 1] = quotient;
        quotient = (uint32_t)(dividend / divisor);
        remainder = (uint32_t)(dividend % divisor);
        mant->digits[i] = remainder;
    }
    dividend = ((uint64_t)remainder * BIGNAT_BASE) + mant->first_digit;
    if (mant->n && mant->digits[mant->n - 1] == 0) mant->n--;
    mant->first_digit = (uint32_t)(dividend / divisor);
}

/* Shift left by a multiple of BIGNAT_NBIT */
static void bignat_lshift_n(struct BigNat *mant, int n) {
    if (!n) return;
    int32_t oldn = mant->n;
    bignat_extra(mant, n);
    memmove(mant->digits + n, mant->digits, sizeof(uint32_t) * oldn);
    memset(mant->digits, 0, sizeof(uint32_t) * (n - 1));
    mant->digits[n - 1] = mant->first_digit;
    mant->first_digit = 0;
}

#ifdef __GNUC__
#define clz(x) __builtin_clz(x)
#else
static int clz(uint32_t x) {
    int n = 0;
    if (x <= 0x0000ffff) n += 16, x <<= 16;
    if (x <= 0x00ffffff) n += 8, x <<= 8;
    if (x <= 0x0fffffff) n += 4, x <<= 4;
    if (x <= 0x3fffffff) n += 2, x <<= 2;
    if (x <= 0x7fffffff) n ++;
    return n;
}
#endif

/* Extract double value from mantissa */
static double bignat_extract(struct BigNat *mant, int32_t exponent2) {
    uint64_t top53;
    int32_t n = mant->n;
    /* Get most significant 53 bits from mant. Bit 52 (0 indexed) should
     * always be 1. This is essentially a large right shift on mant.*/
    if (n) {
        /* Two or more digits */
        uint64_t d1 = mant->digits[n - 1]; /* MSD (non-zero) */
        uint64_t d2 = (n == 1) ? mant->first_digit : mant->digits[n - 2];
        uint64_t d3 = (n > 2) ? mant->digits[n - 3] : (n == 2) ? mant->first_digit : 0;
        int lz = clz((uint32_t) d1);
        int nbits = 32 - lz;
        /* First get 54 bits */
        top53 = (d2 << (54 - BIGNAT_NBIT)) + (d3 >> (2 * BIGNAT_NBIT - 54));
        top53 >>= nbits;
        top53 |= (d1 << (54 - nbits));
        /* Rounding based on lowest bit of 54 */
        if (top53 & 1) top53++;
        top53 >>= 1;
        if (top53 > 0x1FffffFFFFffffUL) {
            top53 >>= 1;
            exponent2++;
        }
        /* Correct exponent - to correct for large right shift to mantissa. */
        exponent2 += (nbits - 53) + BIGNAT_NBIT * n;
    } else {
        /* One digit */
        top53 = mant->first_digit;
    }
    return ldexp((double)top53, exponent2);
}

/* Read in a mantissa and exponent of a certain base, and give
 * back the double value. Should properly handle 0s, infinities, and
 * denormalized numbers. (When the exponent values are too large or small) */
static double convert(
    int negative,
    struct BigNat *mant,
    int32_t base,
    int32_t exponent) {

    int32_t exponent2 = 0;

    /* Approximate exponent in base 2 of mant and exponent. This should get us a good estimate of the final size of the
     * number, within * 2^32 or so. */
    int64_t mant_exp2_approx = mant->n * 32 + 16;
    int64_t exp_exp2_approx = (int64_t)(floor(log2(base) * exponent));
    int64_t exp2_approx = mant_exp2_approx + exp_exp2_approx;

    /* Short circuit zero, huge, and small numbers. We use the exponent range of valid IEEE754 doubles (-1022, 1023)
     * with a healthy buffer to allow for inaccuracies in the approximation and denormailzed numbers. */
    if (mant->n == 0 && mant->first_digit == 0)
        return negative ? -0.0 : 0.0;
    if (exp2_approx > 1176)
        return negative ? -INFINITY : INFINITY;
    if (exp2_approx < -1175)
        return negative ? -0.0 : 0.0;

    /* Final value is X = mant * base ^ exponent * 2 ^ exponent2
     * Get exponent to zero while holding X constant. */

    /* Positive exponents are simple */
    for (; exponent > 3; exponent -= 4) bignat_muladd(mant, base * base * base * base, 0);
    for (; exponent > 1; exponent -= 2) bignat_muladd(mant, base * base, 0);
    for (; exponent > 0; exponent -= 1) bignat_muladd(mant, base, 0);

    /* Negative exponents are tricky - we don't want to loose bits
     * from integer division, so we need to premultiply. */
    if (exponent < 0) {
        int32_t shamt = 5 - exponent / 4;
        bignat_lshift_n(mant, shamt);
        exponent2 -= shamt * BIGNAT_NBIT;
        for (; exponent < -3; exponent += 4) bignat_div(mant, base * base * base * base);
        for (; exponent < -1; exponent += 2) bignat_div(mant, base * base);
        for (; exponent <  0; exponent += 1) bignat_div(mant, base);
    }

    return negative
           ? -bignat_extract(mant, exponent2)
           : bignat_extract(mant, exponent2);
}

/* Scan a real (double) from a string. If the string cannot be converted into
 * and integer, return 0. */
int janet_scan_number_base(
    const uint8_t *str,
    int32_t len,
    int32_t base,
    double *out) {
    const uint8_t *end = str + len;
    int seenadigit = 0;
    int ex = 0;
    int seenpoint = 0;
    int foundexp = 0;
    int neg = 0;
    struct BigNat mant;
    bignat_zero(&mant);

    /* Prevent some kinds of overflow bugs relating to the exponent
     * overflowing.  For example, if a string was passed 2GB worth of 0s after
     * the decimal point, exponent could wrap around and become positive. It's
     * easier to reject ridiculously large inputs than to check for overflows.
     * */
    if (len > INT32_MAX / 40) goto error;

    /* Get sign */
    if (str >= end) goto error;
    if (*str == '-') {
        neg = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* Check for leading 0x or digit digit r */
    if (base == 0) {
        if (str + 1 < end && str[0] == '0' && str[1] == 'x') {
            base = 16;
            str += 2;
        } else if (str + 1 < end  &&
                   str[0] >= '0' && str[0] <= '9' &&
                   str[1] == 'r') {
            base = str[0] - '0';
            str += 2;
        } else if (str + 2 < end  &&
                   str[0] >= '0' && str[0] <= '9' &&
                   str[1] >= '0' && str[1] <= '9' &&
                   str[2] == 'r') {
            base = 10 * (str[0] - '0') + (str[1] - '0');
            if (base < 2 || base > 36) goto error;
            str += 3;
        }
    }

    /* If still base is 0, set to default (10) */
    if (base == 0) {
        base = 10;
    }

    /* Skip leading zeros */
    while (str < end && (*str == '0' || *str == '.')) {
        if (seenpoint) ex--;
        if (*str == '.') {
            if (seenpoint) goto error;
            seenpoint = 1;
        } else {
            seenadigit = 1;
        }
        str++;
    }

    /* Parse significant digits */
    while (str < end) {
        if (*str == '.') {
            if (seenpoint) goto error;
            seenpoint = 1;
        } else if (*str == '&') {
            foundexp = 1;
            break;
        } else if (base == 10 && (*str == 'E' || *str == 'e')) {
            foundexp = 1;
            break;
        } else if (*str == '_') {
            if (!seenadigit) goto error;
        } else {
            int digit = digit_lookup[*str & 0x7F];
            if (*str > 127 || digit >= base) goto error;
            if (seenpoint) ex--;
            bignat_muladd(&mant, base, digit);
            seenadigit = 1;
        }
        str++;
    }

    if (!seenadigit)
        goto error;

    /* Read exponent */
    if (str < end && foundexp) {
        int eneg = 0;
        int32_t ee = 0;
        seenadigit = 0;
        str++;
        if (str >= end) goto error;
        if (*str == '-') {
            eneg = 1;
            str++;
        } else if (*str == '+') {
            str++;
        }
        /* Skip leading 0s in exponent */
        while (str < end && *str == '0') {
            str++;
            seenadigit = 1;
        }
        while (str < end) {
            int digit = digit_lookup[*str & 0x7F];
            if (*str > 127 || digit >= base) goto error;
            if (ee < (INT32_MAX / 40)) {
                ee = base * ee + digit;
            }
            str++;
            seenadigit = 1;
        }
        if (eneg) ex -= ee;
        else ex += ee;
    }

    if (!seenadigit)
        goto error;

    *out = convert(neg, &mant, base, ex);
    janet_free(mant.digits);
    return 0;

error:
    janet_free(mant.digits);
    return 1;
}

int janet_scan_number(
    const uint8_t *str,
    int32_t len,
    double *out) {
    return janet_scan_number_base(str, len, 0, out);
}

#ifdef JANET_INT_TYPES

static int scan_uint64(
    const uint8_t *str,
    int32_t len,
    uint64_t *out,
    int *neg) {
    const uint8_t *end = str + len;
    int seenadigit = 0;
    int base = 10;
    *neg = 0;
    *out = 0;
    uint64_t accum = 0;
    /* len max is INT64_MAX in base 2 with _ between each bits */
    /* '2r' + 64 bits + 63 _  + sign = 130 => 150 for some leading  */
    /* zeros */
    if (len > 150) return 0;
    /* Get sign */
    if (str >= end) return 0;
    if (*str == '-') {
        *neg = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    /* Check for leading 0x or digit digit r */
    if (str + 1 < end && str[0] == '0' && str[1] == 'x') {
        base = 16;
        str += 2;
    } else if (str + 1 < end  &&
               str[0] >= '0' && str[0] <= '9' &&
               str[1] == 'r') {
        base = str[0] - '0';
        str += 2;
    } else if (str + 2 < end  &&
               str[0] >= '0' && str[0] <= '9' &&
               str[1] >= '0' && str[1] <= '9' &&
               str[2] == 'r') {
        base = 10 * (str[0] - '0') + (str[1] - '0');
        if (base < 2 || base > 36) return 0;
        str += 3;
    }

    /* Skip leading zeros */
    while (str < end && *str == '0') {
        seenadigit = 1;
        str++;
    }
    /* Parse significant digits */
    while (str < end) {
        if (*str == '_') {
            if (!seenadigit) return 0;
        } else {
            int digit = digit_lookup[*str & 0x7F];
            if (*str > 127 || digit >= base) return 0;
            if (accum > (UINT64_MAX - digit) / base) return 0;
            accum = accum * base + digit;
            seenadigit = 1;
        }
        str++;
    }

    if (!seenadigit) return 0;
    *out = accum;
    return 1;
}

int janet_scan_int64(const uint8_t *str, int32_t len, int64_t *out) {
    int neg;
    uint64_t bi;
    if (scan_uint64(str, len, &bi, &neg)) {
        if (neg && bi <= ((UINT64_MAX / 2) + 1)) {
            if (bi > INT64_MAX) {
                *out = INT64_MIN;
            } else {
                *out = -((int64_t) bi);
            }
            return 1;
        }
        if (!neg && bi <= INT64_MAX) {
            *out = (int64_t) bi;
            return 1;
        }
    }
    return 0;
}

int janet_scan_uint64(const uint8_t *str, int32_t len, uint64_t *out) {
    int neg;
    uint64_t bi;
    if (scan_uint64(str, len, &bi, &neg)) {
        if (!neg) {
            *out = bi;
            return 1;
        }
    }
    return 0;
}

/* Similar to janet_scan_number but allows for
 * more numeric types with a given suffix. */
int janet_scan_numeric(
    const uint8_t *str,
    int32_t len,
    Janet *out) {
    int result;
    double num;
    int64_t i64 = 0;
    uint64_t u64 = 0;
    if (len < 2 || str[len - 2] != ':') {
        result = janet_scan_number_base(str, len, 0, &num);
        *out = janet_wrap_number(num);
        return result;
    }
    switch (str[len - 1]) {
        default:
            return 1;
        case 'n':
            result = janet_scan_number_base(str, len - 2, 0, &num);
            *out = janet_wrap_number(num);
            return result;
        /* Condition is inverted janet_scan_int64 and janet_scan_uint64 */
        case 's':
            result = !janet_scan_int64(str, len - 2, &i64);
            *out = janet_wrap_s64(i64);
            return result;
        case 'u':
            result = !janet_scan_uint64(str, len - 2, &u64);
            *out = janet_wrap_u64(u64);
            return result;
    }
}

#endif

void janet_buffer_dtostr(JanetBuffer *buffer, double x) {
#define BUFSIZE 32
    janet_buffer_extra(buffer, BUFSIZE);
    int count = snprintf((char *) buffer->data + buffer->count, BUFSIZE, "%.17g", x);
#undef BUFSIZE
    /* fix locale issues with commas */
    for (int i = 0; i < count; i++) {
        char c = buffer->data[buffer->count + i];
        if (c == ',') {
            buffer->data[buffer->count + i] = '.';
        }
    }
    buffer->count += count;
}
