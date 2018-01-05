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

/* Use a custom double parser instead of libc's strtod for better portability
 * and control. Also, uses a less strict rounding method than ieee to not incur
 * the cost of 4000 loc and dependence on arbitary precision arithmetic.  There
 * is no plan to use arbitrary precision arithmetic for parsing numbers, and a
 * formal rounding mode has yet to be chosen (round towards 0 seems
 * reasonable).
 *
 * This version has been modified for much greater flexibility in parsing, such
 * as choosing the radix, supporting integer output, and returning DstValues
 * directly. 
 *
 * Numbers are of the form [-+]R[rR]I.F[eE&][-+]X where R is the radix, I is
 * the integer part, F is the fractional part, and X is the exponent. All
 * signs, radix, decimal point, fractional part, and exponent can be ommited.
 * The number will be considered and integer if the there is no decimal point
 * and no exponent. Any number greater the 2^32-1 or less than -(2^32) will be
 * coerced to a double. If there is an error, the function dst_scan_number will
 * return a dst nil. The radix is assumed to be 10 if omitted, and the E
 * separator for the exponent can only be used when the radix is 10. This is
 * because E is a vaid digit in bases 15 or greater. For bases greater than 10,
 * the letters are used as digitis. A through Z correspond to the digits 10
 * through 35, and the lowercase letters have the same values. The radix number
 * is always in base 10. For example, a hexidecimal number could be written
 * '16rdeadbeef'. dst_scan_number also supports some c style syntax for
 * hexidecimal literals. The previous number could also be written
 * '0xdeadbeef'. Note that in this case, the number will actually be a double
 * as it will not fit in the range for a signed 32 bit integer. The string
 * '0xbeef' would parse to an integer as it is in the range of an int32_t. */

#include "unit.h"
#include <dst/dst.h>
#include <math.h>

DstValue dst_scan_number(const uint8_t *str, int32_t len);

const char *valid_test_strs[] = {
    "0",
    "-0.0",
    "+0",
    "123",
    "-123",
    "aaaaaa",
    "+a123",
    "0.12312",
    "89.12312",
    "-123.01231",
    "123e10",
    "1203412347981232379183.13013248723478932478923478e12",
    "120341234798123237918313013248723478932478923478",
    "999_999_999_999",
    "8r777",
    "",
    "----",
    "   ",
    "--123",
    "0xff",
    "0xff.f",
    "0xff&-1",
    "0xfefefe",
    "1926.4823e11",
    "0xff_ff_ff_ff",
    "0xff_ff_ff_ff_ff_ff",
    "2r1010",
    "2r10101010001101",
    "123a",
    "0.1e510",
    "4.123123e-308",
    "4.123123e-320",
    "1e-308",
    "1e-309",
    "9e-308",
    "9e-309",
    "919283691283e-309",
    "9999e302",
    "123.12312.123",
    "90.e0.1",
    "90.e1",
    ".e1"
};

int main() {
    dst_init();
    unsigned i;
    for (i = 0; i < (sizeof(valid_test_strs) / sizeof(void *)); i++) {
        DstValue out;
        double refout;
        const uint8_t *str = (const uint8_t *) valid_test_strs[i];
        int32_t len = 0; while (str[len]) len++;

        refout = strtod(valid_test_strs[i], NULL);
        out = dst_scan_number(str, len);
        dst_puts(dst_formatc("literal: %s, out: %v, refout: %v\n", 
                    valid_test_strs[i], out, dst_wrap_real(refout)));

    }
        uint64_t x = 0x07FFFFFFFFFFFFFF;
        uint64_t y = 36;

        printf("%llu, %llu\n", x, (x * y) / y);
}
