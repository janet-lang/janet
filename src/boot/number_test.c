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

#include <janet.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "tests.h"

/* Check a subset of numbers against system implementation.
 * Note that this depends on the system implementation being correct,
 * which may not be the case for old or non compliant systems. Also,
 * we cannot check against bases other 10. */

/* Compare valid c numbers to system implementation. */
static void test_valid_str(const char *str) {
    int err;
    double cnum, jnum;
    jnum = 0.0;
    cnum = atof(str);
    err = janet_scan_number((const uint8_t *) str, (int32_t) strlen(str), &jnum);
    assert(!err);
    assert(cnum == jnum);
}

int number_test() {

    test_valid_str("1.0");
    test_valid_str("1");
    test_valid_str("2.1");
    test_valid_str("1e10");
    test_valid_str("2e10");
    test_valid_str("1e-10");
    test_valid_str("2e-10");
    test_valid_str("1.123123e10");
    test_valid_str("1.123123e-10");
    test_valid_str("-1.23e2");
    test_valid_str("-4.5e15");
    test_valid_str("-4.5e151");
    test_valid_str("-4.5e200");
    test_valid_str("-4.5e123");
    test_valid_str("123123123123123123132123");
    test_valid_str("0000000011111111111111111111111111");
    test_valid_str(".112312333333323123123123123123123");

    return 0;
}
