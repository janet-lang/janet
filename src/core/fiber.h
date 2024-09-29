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

#ifndef JANET_FIBER_H_defined
#define JANET_FIBER_H_defined

#ifndef JANET_AMALG
#include <janet.h>
#endif

/* Fiber signal masks. */
#define JANET_FIBER_MASK_ERROR 2
#define JANET_FIBER_MASK_DEBUG 4
#define JANET_FIBER_MASK_YIELD 8

#define JANET_FIBER_MASK_USER0 (16 << 0)
#define JANET_FIBER_MASK_USER1 (16 << 1)
#define JANET_FIBER_MASK_USER2 (16 << 2)
#define JANET_FIBER_MASK_USER3 (16 << 3)
#define JANET_FIBER_MASK_USER4 (16 << 4)
#define JANET_FIBER_MASK_USER5 (16 << 5)
#define JANET_FIBER_MASK_USER6 (16 << 6)
#define JANET_FIBER_MASK_USER7 (16 << 7)
#define JANET_FIBER_MASK_USER8 (16 << 8)
#define JANET_FIBER_MASK_USER9 (16 << 9)

#define JANET_FIBER_MASK_USERN(N) (16 << (N))
#define JANET_FIBER_MASK_USER 0x3FF0

#define JANET_FIBER_STATUS_MASK 0x3F0000
#define JANET_FIBER_RESUME_SIGNAL 0x400000
#define JANET_FIBER_STATUS_OFFSET 16

#define JANET_FIBER_BREAKPOINT       0x1000000
#define JANET_FIBER_RESUME_NO_USEVAL 0x2000000
#define JANET_FIBER_RESUME_NO_SKIP   0x4000000
#define JANET_FIBER_DID_LONGJUMP     0x8000000
#define JANET_FIBER_FLAG_MASK        0xF000000

#define JANET_FIBER_EV_FLAG_CANCELED 0x10000
#define JANET_FIBER_EV_FLAG_SUSPENDED 0x20000
#define JANET_FIBER_FLAG_ROOT 0x40000
#define JANET_FIBER_EV_FLAG_IN_FLIGHT 0x1

/* used only on windows, should otherwise be unset */

#define janet_fiber_set_status(f, s) do {\
    (f)->flags &= ~JANET_FIBER_STATUS_MASK;\
    (f)->flags |= (s) << JANET_FIBER_STATUS_OFFSET;\
} while (0)

#define janet_stack_frame(s) ((JanetStackFrame *)((s) - JANET_FRAME_SIZE))
#define janet_fiber_frame(f) janet_stack_frame((f)->data + (f)->frame)
void janet_fiber_setcapacity(JanetFiber *fiber, int32_t n);
void janet_fiber_push(JanetFiber *fiber, Janet x);
void janet_fiber_push2(JanetFiber *fiber, Janet x, Janet y);
void janet_fiber_push3(JanetFiber *fiber, Janet x, Janet y, Janet z);
void janet_fiber_pushn(JanetFiber *fiber, const Janet *arr, int32_t n);
int janet_fiber_funcframe(JanetFiber *fiber, JanetFunction *func);
int janet_fiber_funcframe_tail(JanetFiber *fiber, JanetFunction *func);
void janet_fiber_cframe(JanetFiber *fiber, JanetCFunction cfun);
void janet_fiber_popframe(JanetFiber *fiber);
void janet_env_maybe_detach(JanetFuncEnv *env);
int janet_env_valid(JanetFuncEnv *env);

#ifdef JANET_EV
void janet_fiber_did_resume(JanetFiber *fiber);
#endif

#endif
