/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_AMALG
#include <janet.h>
#include "gc.h"
#endif

/* Create new userdata */
void *janet_abstract_begin(const JanetAbstractType *atype, size_t size) {
    JanetAbstractHead *header = janet_gcalloc(JANET_MEMORY_NONE,
                                sizeof(JanetAbstractHead) + size);
    header->size = size;
    header->type = atype;
    return (void *) & (header->data);
}

void *janet_abstract_end(void *x) {
    janet_gc_settype((void *)(janet_abstract_head(x)), JANET_MEMORY_ABSTRACT);
    return x;
}

void *janet_abstract(const JanetAbstractType *atype, size_t size) {
    return janet_abstract_end(janet_abstract_begin(atype, size));
}
