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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "state.h"
#include "util.h"
#endif

#ifdef JANET_WINDOWS
#include <windows.h>
#endif

JANET_THREAD_LOCAL JanetVM janet_vm;

JanetVM *janet_local_vm(void) {
    return &janet_vm;
}

JanetVM *janet_vm_alloc(void) {
    JanetVM *mem = janet_malloc(sizeof(JanetVM));
    if (NULL == mem) {
        JANET_OUT_OF_MEMORY;
    }
    return mem;
}

void janet_vm_free(JanetVM *vm) {
    janet_free(vm);
}

void janet_vm_save(JanetVM *into) {
    *into = janet_vm;
}

void janet_vm_load(JanetVM *from) {
    janet_vm = *from;
}

/* Trigger suspension of the Janet vm by trying to
 * exit the interpreter loop when convenient. You can optionally
 * use NULL to interrupt the current VM when convenient */
void janet_interpreter_interrupt(JanetVM *vm) {
    vm = vm ? vm : &janet_vm;
    janet_atomic_inc(&vm->auto_suspend);
}

void janet_interpreter_interrupt_handled(JanetVM *vm) {
    vm = vm ? vm : &janet_vm;
    janet_atomic_dec(&vm->auto_suspend);
}
