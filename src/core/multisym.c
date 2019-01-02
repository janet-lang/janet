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

#include <janet/janet.h>
#include "compile.h"
#include "emit.h"
#include "vector.h"

/* Parse a part of a symbol that can be used for building up code. */
static JanetSlot multisym_parse_part(JanetCompiler *c, const uint8_t *sympart, int32_t len) {
    if (sympart[0] == ':') {
        return janetc_cslot(janet_symbolv(sympart, len));
    } else {
        double index;
        int err;
        index = janet_scan_number(sympart + 1, len - 1, &err);
        if (err) {
            /* not a number */
            return janetc_resolve(c, janet_symbol(sympart + 1, len - 1));
        } else {
            /* is a number */
            return janetc_cslot(janet_wrap_number(index));
        }
    }
}

static JanetSlot multisym_do_parts(JanetFopts opts, int put, const uint8_t *sym, Janet rvalue) {
    JanetSlot slot;
    JanetFopts subopts = janetc_fopts_default(opts.compiler);
    int i, j;
    for (i = 1, j = 0; sym[i]; i++) {
        if (sym[i] == ':' || sym[i] == '.') {
            if (j) {
                JanetSlot target = janetc_gettarget(subopts);
                JanetSlot value = multisym_parse_part(opts.compiler, sym + j, i - j);
                janetc_emit_sss(opts.compiler, JOP_GET, target, slot, value, 1);
                slot = target;
            } else {
                const uint8_t *nextsym = janet_symbol(sym + j, i - j);
                slot = janetc_resolve(opts.compiler, nextsym);
            }
            j = i;
        }
    }

    if (j) {
        /* multisym (outermost get or put) */
        JanetSlot target = janetc_gettarget(opts);
        JanetSlot key = multisym_parse_part(opts.compiler, sym + j, i - j);
        if (put) {
            subopts.flags = JANET_FOPTS_HINT;
            subopts.hint = target;
            JanetSlot r_slot = janetc_value(subopts, rvalue);
            janetc_emit_sss(opts.compiler, JOP_PUT, slot, key, r_slot, 0);
            janetc_copy(opts.compiler, target, r_slot);
        } else {
            janetc_emit_sss(opts.compiler, JOP_GET, target, slot, key, 1);
        }
        return target;
    } else {
        /* normal symbol */
        if (put) {
            JanetSlot ret, dest;
            dest = janetc_resolve(opts.compiler, sym);
            if (!(dest.flags & JANET_SLOT_MUTABLE)) {
                janetc_cerror(opts.compiler, "cannot set constant");
                return janetc_cslot(janet_wrap_nil());
            }
            subopts.flags = JANET_FOPTS_HINT;
            subopts.hint = dest;
            ret = janetc_value(subopts, rvalue);
            janetc_copy(opts.compiler, dest, ret);
            return ret;
        }
        return janetc_resolve(opts.compiler, sym);
    }
}

/* Check if a symbol is a multisym, and if so, transform
 * it and emit the code for treating it as a bunch of nested
 * gets. */
JanetSlot janetc_sym_rvalue(JanetFopts opts, const uint8_t *sym) {
    if (janet_string_length(sym) && sym[0] != ':') {
        return multisym_do_parts(opts, 0, sym, janet_wrap_nil());
    } else {
        /* keyword */
        return janetc_cslot(janet_wrap_symbol(sym));
    }
}

/* Check if a symbol is a multisym, and if so, transform 
 * it into the correct 'put' expression. */
JanetSlot janetc_sym_lvalue(JanetFopts opts, const uint8_t *sym, Janet value) {
    return multisym_do_parts(opts, 1, sym, value);
}
