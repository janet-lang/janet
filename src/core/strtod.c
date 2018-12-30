/*
* Copyright (c) 2018 Calvin Rose
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
 * and control. Also, uses a less strict rounding method than ieee to not incur
 * the cost of 4000 loc and dependence on arbitary precision arithmetic. Includes
 * subset of multiple precision functionality needed for correct rounding.
 *
 * This version has been modified for much greater flexibility in parsing, such
 * as choosing the radix and supporting scientific notation with any radix.
 *
 * Numbers are of the form [-+]R[rR]I.F[eE&][-+]X where R is the radix, I is
 * the integer part, F is the fractional part, and X is the exponent. All
 * signs, radix, decimal point, fractional part, and exponent can be ommited.
 * The number will be considered and integer if the there is no decimal point
 * and no exponent. Any number greater the 2^32-1 or less than -(2^32) will be
 * coerced to a double. If there is an error, the function janet_scan_number will
 * return a janet nil. The radix is assumed to be 10 if omitted, and the E
 * separator for the exponent can only be used when the radix is 10. This is
 * because E is a vaid digit in bases 15 or greater. For bases greater than 10,
 * the letters are used as digitis. A through Z correspond to the digits 10
 * through 35, and the lowercase letters have the same values. The radix number
 * is always in base 10. For example, a hexidecimal number could be written
 * '16rdeadbeef'. janet_scan_number also supports some c style syntax for
 * hexidecimal literals. The previous number could also be written
 * '0xdeadbeef'. Note that in this case, the number will actually be a double
 * as it will not fit in the range for a signed 32 bit integer. The string
 * '0xbeef' would parse to an integer as it is in the range of an int32_t. */

#include <janet/janet.h>
#include <math.h>
#include <string.h>

/* Lookup table for getting values of characters when parsing numbers. Handles
 * digits 0-9 and a-z (and A-Z). A-Z have values of 10 to 35. */
static uint8_t digit_lookup[128] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0,1,2,3,4,5,6,7,8,9,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,0xff,0xff,0xff,0xff,0xff,
    0xff,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,0xff,0xff,0xff,0xff,0xff
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
        uint32_t *mem = realloc(mant->digits, newcap * sizeof(uint32_t));
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

/* Add term to mant */
static void bignat_add(struct BigNat *mant, uint32_t dig) {
    int32_t i;
    int carry = 0;
    uint32_t next = mant->first_digit + dig;
    if (next >= BIGNAT_BASE) {
        next -= BIGNAT_BASE;
        carry = 1;
    }
    mant->first_digit = next;
    for (i = 0; i < mant->n; i++) {
        if (!carry) return;
        uint32_t next = mant->digits[i] + 1;
        if (next >= BIGNAT_BASE) {
            next = 0;
        } else {
            carry = 0;
        }
        mant->digits[i] = next;
    }
    if (carry) bignat_append(mant, 1);
}

/* Multiply the mantissa mant by a factor */
static void bignat_mul(struct BigNat *mant, uint32_t factor) {
    int32_t i;
    uint64_t carry = ((uint64_t) mant->first_digit) * factor;
    mant->first_digit = carry % BIGNAT_BASE;
    carry /= BIGNAT_BASE;
    for (i = 0; i < mant->n; i++) {
        carry += ((uint64_t) mant->digits[i]) * factor;
        mant->digits[i] = carry % BIGNAT_BASE;
        carry /= BIGNAT_BASE;
    }
    if (carry) bignat_append(mant, carry);
}

/* Divide the mantissa mant by a factor. Drop the remainder. */
static void bignat_div(struct BigNat *mant, uint32_t divisor) {
    int32_t i;
    uint32_t quotient, remainder;
    uint64_t dividend;
    remainder = 0;
    for (i = mant->n - 1; i >= 0; i--) {
        dividend = ((uint64_t)remainder * BIGNAT_BASE) + mant->digits[i];
        if (i < mant->n - 1) mant->digits[i + 1] = quotient;
        quotient = dividend / divisor;
        remainder = dividend % divisor;
        mant->digits[i] = remainder;
    }
    dividend = ((uint64_t)remainder * BIGNAT_BASE) + mant->first_digit;
    if (mant->n && mant->digits[mant->n - 1] == 0) mant->n--;
    mant->first_digit = dividend / divisor;
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
    /* Get most significant 53 bits from mant. Bit52 (0 indexed) should
     * always be 1. This is essentially a large right shift on mant.*/
    if (n) {
        /* Two or more digits */
        uint64_t d1 = mant->digits[n - 1]; /* MSD (non-zero) */
        uint64_t d2 = (n == 1) ? mant->first_digit : mant->digits[n - 2];
        uint64_t d3 = (n > 2) ? mant->digits[n - 3] : (n == 2) ? mant->first_digit : 0;
        int lz = clz(d1);
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
    return ldexp(top53, exponent2);
}

/* Read in a mantissa and exponent of a certain base, and give
 * back the double value. Should properly handle 0s, Infinities, and
 * denormalized numbers. (When the exponent values are too large) */
static double convert(
        int negative,
        struct BigNat *mant,
        int32_t base,
        int32_t exponent) {

    int32_t exponent2 = 0;

    /* Short circuit zero and huge numbers */
    if (mant->n == 0 && mant->first_digit == 0)
        return negative ? -0.0 : 0.0;
    if (exponent > 1023)
        return negative ? -INFINITY : INFINITY;

    /* Final value is X = mant * base ^ exponent * 2 ^ exponent2
     * Get exponent to zero while holding X constant. */

    /* Positive exponents are simple */
    while (exponent > 0) {
        bignat_mul(mant, base);
        exponent--;
    }

    /* Negative exponents are tricky - we don't want to loose bits
     * from integer division, so we need to premultiply. */
    if (exponent < 0) {
        int32_t shamt = 5 - exponent / 4;
        bignat_lshift_n(mant, shamt);
        exponent2 -= shamt * BIGNAT_NBIT;
        while (exponent < 0) {
            bignat_div(mant, base);
            exponent++;
        }
    }

    return negative
        ? -bignat_extract(mant, exponent2)
        : bignat_extract(mant, exponent2);
}

/* Scan a real (double) from a string. If the string cannot be converted into
 * and integer, set *err to 1 and return 0. */
double janet_scan_number(
        const uint8_t *str,
        int32_t len,
        int *err) {
    const uint8_t *end = str + len;
    int seenadigit = 0;
    int ex = 0;
    int base = 10;
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
    if (str + 1 < end && str[0] == '0' && str[1] == 'x') {
        base = 16;
        str += 2;
    } else if (str + 2 < end  &&
            str[0] >= '0' && str[0] <= '9' &&
            str[1] >= '0' && str[1] <= '9' &&
            str[2] == 'r') {
        base = 10 * (str[0] - '0') + (str[1] - '0');
        if (base < 2 || base > 36) goto error;
        str += 3;
    }

    /* Skip leading zeros */
    while (str < end && (*str == '0' || *str == '.')) {
        if (seenpoint) ex--;
        if (*str == '.') {
            if (seenpoint) goto error;
            seenpoint = 1;
        }
        seenadigit = 1;
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
        } else if (*str != '_') {
            /* underscores are ignored - can be used for separator */
            int digit = digit_lookup[*str & 0x7F];
            if (*str > 127 || digit >= base) goto error;
            if (seenpoint) ex--;
            bignat_mul(&mant, base);
            bignat_add(&mant, digit);
            seenadigit = 1;
        }
        str++;
    }

    if (!seenadigit)
        goto error;

    /* Read exponent */
    if (str < end && foundexp) {
        int eneg = 0;
        int ee = 0;
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
        while (str < end && ee < (INT32_MAX / 40)) {
            int digit = digit_lookup[*str & 0x7F];
            if (*str == '_') {
                str++;
                continue;
            }
            if (*str > 127 || digit >= base) goto error;
            ee = base * ee + digit;
            str++;
            seenadigit = 1;
        }
        if (eneg) ex -= ee; else ex += ee;
    }

    if (!seenadigit)
        goto error;

    double result = convert(neg, &mant, base, ex);
    free(mant.digits);
    *err = 0;
    return result;

    error:
    *err = 1;
    free(mant.digits);
    return 0.0;
}
