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

#ifndef DST_H_defined
#define DST_H_defined

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dstconfig.h"
#include "dsttypes.h"
#include "dststate.h"

/* Array functions */
DstArray *dst_array(int32_t capacity);
DstArray *dst_array_init(DstArray *array, int32_t capacity);
void dst_array_deinit(DstArray *array);
void dst_array_ensure(DstArray *array, int32_t capacity);
void dst_array_setcount(DstArray *array, int32_t count);
void dst_array_push(DstArray *array, DstValue x);
DstValue dst_array_pop(DstArray *array);
DstValue dst_array_peek(DstArray *array);

/* Buffer functions */
DstBuffer *dst_buffer(int32_t capacity);
DstBuffer *dst_buffer_init(DstBuffer *buffer, int32_t capacity);
void dst_buffer_deinit(DstBuffer *buffer);
void dst_buffer_ensure(DstBuffer *buffer, int32_t capacity);
void dst_buffer_extra(DstBuffer *buffer, int32_t n);
void dst_buffer_push_bytes(DstBuffer *buffer, const uint8_t *string, int32_t len);
void dst_buffer_push_cstring(DstBuffer *buffer, const char *cstring);
void dst_buffer_push_u8(DstBuffer *buffer, uint8_t x);
void dst_buffer_push_u16(DstBuffer *buffer, uint16_t x);
void dst_buffer_push_u32(DstBuffer *buffer, uint32_t x);
void dst_buffer_push_u64(DstBuffer *buffer, uint64_t x);

/* Tuple */
#define dst_tuple_raw(t) ((int32_t *)(t) - 2)
#define dst_tuple_length(t) (dst_tuple_raw(t)[0])
#define dst_tuple_hash(t) ((dst_tuple_raw(t)[1]))
DstValue *dst_tuple_begin(int32_t length);
const DstValue *dst_tuple_end(DstValue *tuple);
const DstValue *dst_tuple_n(DstValue *values, int32_t n);
int dst_tuple_equal(const DstValue *lhs, const DstValue *rhs);
int dst_tuple_compare(const DstValue *lhs, const DstValue *rhs);

/* String/Symbol functions */
#define dst_string_raw(s) ((int32_t *)(s) - 2)
#define dst_string_length(s) (dst_string_raw(s)[0])
#define dst_string_hash(s) ((dst_string_raw(s)[1]))
uint8_t *dst_string_begin(int32_t length);
const uint8_t *dst_string_end(uint8_t *str);
const uint8_t *dst_string(const uint8_t *buf, int32_t len);
const uint8_t *dst_cstring(const char *cstring);
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs);
int dst_string_equal(const uint8_t *lhs, const uint8_t *rhs);
int dst_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash);
const uint8_t *dst_string_unique(const uint8_t *buf, int32_t len);
const uint8_t *dst_cstring_unique(const char *s);
const uint8_t *dst_description(DstValue x);
const uint8_t *dst_short_description(DstValue x);
const uint8_t *dst_to_string(DstValue x);
#define dst_cstringv(cstr) dst_wrap_string(dst_cstring(cstr))
#define dst_stringv(str, len) dst_wrap_string(dst_string((str), (len)))
const uint8_t *dst_formatc(const char *format, ...);
void dst_puts(const uint8_t *str);

/* Symbol functions */
const uint8_t *dst_symbol(const uint8_t *str, int32_t len);
const uint8_t *dst_symbol_from_string(const uint8_t *str);
const uint8_t *dst_csymbol(const char *str);
const uint8_t *dst_symbol_gen(const uint8_t *buf, int32_t len);
#define dst_symbolv(str, len) dst_wrap_symbol(dst_symbol((str), (len)))
#define dst_csymbolv(cstr) dst_wrap_symbol(dst_csymbol(cstr))

/* Structs */
#define dst_struct_raw(t) ((int32_t *)(t) - 2)
#define dst_struct_length(t) (dst_struct_raw(t)[0])
#define dst_struct_capacity(t) (dst_struct_length(t) * 4)
#define dst_struct_hash(t) ((dst_struct_raw(t)[1]))
DstValue *dst_struct_begin(int32_t count);
void dst_struct_put(DstValue *st, DstValue key, DstValue value);
const DstValue *dst_struct_end(DstValue *st);
DstValue dst_struct_get(const DstValue *st, DstValue key);
DstValue dst_struct_next(const DstValue *st, DstValue key);
DstTable *dst_struct_to_table(const DstValue *st);
int dst_struct_equal(const DstValue *lhs, const DstValue *rhs);
int dst_struct_compare(const DstValue *lhs, const DstValue *rhs);

/* Table functions */
DstTable *dst_table(int32_t capacity);
DstTable *dst_table_init(DstTable *table, int32_t capacity);
void dst_table_deinit(DstTable *table);
DstValue dst_table_get(DstTable *t, DstValue key);
DstValue dst_table_remove(DstTable *t, DstValue key);
void dst_table_put(DstTable *t, DstValue key, DstValue value);
DstValue dst_table_next(DstTable *t, DstValue key);
const DstValue *dst_table_to_struct(DstTable *t);
void dst_table_merge(DstTable *t,  DstValue other);

/* Fiber */
DstFiber *dst_fiber(int32_t capacity);
#define dst_stack_frame(s) ((DstStackFrame *)((s) - DST_FRAME_SIZE))
#define dst_fiber_frame(f) dst_stack_frame((f)->data + (f)->frame)
DstFiber *dst_fiber_reset(DstFiber *fiber);
void dst_fiber_setcapacity(DstFiber *fiber, int32_t n);
void dst_fiber_push(DstFiber *fiber, DstValue x);
void dst_fiber_push2(DstFiber *fiber, DstValue x, DstValue y);
void dst_fiber_push3(DstFiber *fiber, DstValue x, DstValue y, DstValue z);
void dst_fiber_pushn(DstFiber *fiber, const DstValue *arr, int32_t n);
DstValue dst_fiber_popvalue(DstFiber *fiber);
void dst_fiber_funcframe(DstFiber *fiber, DstFunction *func);
void dst_fiber_funcframe_tail(DstFiber *fiber, DstFunction *func);
void dst_fiber_cframe(DstFiber *fiber);
void dst_fiber_popframe(DstFiber *fiber);

/* Functions */
void dst_function_detach(DstFunction *func);

/* Assembly */
DstAssembleResult dst_asm(DstAssembleOptions opts);
DstFunction *dst_asm_func(DstAssembleResult result);
DstValue dst_disasm(DstFuncDef *def);
DstValue dst_asm_decode_instruction(uint32_t instr);

/* Treat similar types through uniform interfaces for iteration */
int dst_seq_view(DstValue seq, const DstValue **data, int32_t *len);
int dst_chararray_view(DstValue str, const uint8_t **data, int32_t *len);
int dst_hashtable_view(DstValue tab, const DstValue **data, int32_t *len, int32_t *cap);

/* Abstract */
#define dst_abstract_header(u) ((DstAbstractHeader *)(u) - 1)
#define dst_abstract_type(u) (dst_abstract_header(u)->type)
#define dst_abstract_size(u) (dst_abstract_header(u)->size)

/* Value functions */
int dst_equals(DstValue x, DstValue y);
int32_t dst_hash(DstValue x);
int dst_compare(DstValue x, DstValue y);
DstValue dst_get(DstValue ds, DstValue key);
void dst_put(DstValue ds, DstValue key, DstValue value);
DstValue dst_next(DstValue ds, DstValue key);
int32_t dst_length(DstValue x);
int32_t dst_capacity(DstValue x);
DstValue dst_getindex(DstValue ds, int32_t index);
void dst_setindex(DstValue ds, DstValue value, int32_t index);

/* Utils */
extern const char dst_base64[65];
int32_t dst_array_calchash(const DstValue *array, int32_t len);
int32_t dst_string_calchash(const uint8_t *str, int32_t len);
DstValue dst_loadreg(DstReg *regs, size_t count);
DstValue dst_scan_number(const uint8_t *src, int32_t len);
int32_t dst_scan_integer(const uint8_t *str, int32_t len, int *err);
double dst_scan_real(const uint8_t *str, int32_t len, int *err);

/* Parsing */
DstParseResult dst_parse(const uint8_t *src, int32_t len);
DstParseResult dst_parsec(const char *src);

/* VM functions */
int dst_init();
void dst_deinit();
int dst_run(DstValue callee, DstValue *returnreg);

/* Compile */
DstCompileResult dst_compile(DstCompileOptions opts);
DstFunction *dst_compile_func(DstCompileResult result);

/* STL */
#define DST_LOAD_ROOT 1
DstValue dst_loadstl(int flags);

/* GC */
void dst_mark(DstValue x);
void dst_sweep();
void dst_collect();
void dst_clear_memory();
void dst_gcroot(DstValue root);
int dst_gcunroot(DstValue root);
int dst_gcunrootall(DstValue root);
#define dst_maybe_collect() do {\
    if (dst_vm_next_collection >= dst_vm_gc_interval) dst_collect(); } while (0)

#endif /* DST_H_defined */
