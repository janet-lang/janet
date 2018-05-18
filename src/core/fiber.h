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

#ifndef DST_FIBER_H_defined
#define DST_FIBER_H_defined

#include <dst/dst.h>

extern DST_THREAD_LOCAL DstFiber *dst_vm_fiber;

#define dst_fiber_set_status(f, s) do {\
    (f)->flags &= ~DST_FIBER_STATUS_MASK;\
    (f)->flags |= (s) << DST_FIBER_STATUS_OFFSET;\
} while (0)

#define dst_stack_frame(s) ((DstStackFrame *)((s) - DST_FRAME_SIZE))
#define dst_fiber_frame(f) dst_stack_frame((f)->data + (f)->frame)
DstFiber *dst_fiber_reset(DstFiber *fiber, DstFunction *callee);
void dst_fiber_setcapacity(DstFiber *fiber, int32_t n);
void dst_fiber_push(DstFiber *fiber, Dst x);
void dst_fiber_push2(DstFiber *fiber, Dst x, Dst y);
void dst_fiber_push3(DstFiber *fiber, Dst x, Dst y, Dst z);
void dst_fiber_pushn(DstFiber *fiber, const Dst *arr, int32_t n);
void dst_fiber_funcframe(DstFiber *fiber, DstFunction *func);
void dst_fiber_funcframe_tail(DstFiber *fiber, DstFunction *func);
void dst_fiber_cframe(DstFiber *fiber);
void dst_fiber_popframe(DstFiber *fiber);

#endif
