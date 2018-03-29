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

#ifdef __cplusplus
extern "C" {
#endif

#include "dstconfig.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dsttypes.h"
#include "dststate.h"

/* Array functions */
DstArray *dst_array(int32_t capacity);
DstArray *dst_array_init(DstArray *array, int32_t capacity);
void dst_array_deinit(DstArray *array);
void dst_array_ensure(DstArray *array, int32_t capacity);
void dst_array_setcount(DstArray *array, int32_t count);
void dst_array_push(DstArray *array, Dst x);
Dst dst_array_pop(DstArray *array);
Dst dst_array_peek(DstArray *array);

/* Buffer functions */
DstBuffer *dst_buffer(int32_t capacity);
DstBuffer *dst_buffer_init(DstBuffer *buffer, int32_t capacity);
void dst_buffer_deinit(DstBuffer *buffer);
void dst_buffer_ensure(DstBuffer *buffer, int32_t capacity);
int dst_buffer_extra(DstBuffer *buffer, int32_t n);
int dst_buffer_push_bytes(DstBuffer *buffer, const uint8_t *string, int32_t len);
int dst_buffer_push_cstring(DstBuffer *buffer, const char *cstring);
int dst_buffer_push_u8(DstBuffer *buffer, uint8_t x);
int dst_buffer_push_u16(DstBuffer *buffer, uint16_t x);
int dst_buffer_push_u32(DstBuffer *buffer, uint32_t x);
int dst_buffer_push_u64(DstBuffer *buffer, uint64_t x);

/* Tuple */
#define dst_tuple_raw(t) ((int32_t *)(t) - 2)
#define dst_tuple_length(t) (dst_tuple_raw(t)[0])
#define dst_tuple_hash(t) ((dst_tuple_raw(t)[1]))
Dst *dst_tuple_begin(int32_t length);
const Dst *dst_tuple_end(Dst *tuple);
const Dst *dst_tuple_n(Dst *values, int32_t n);
int dst_tuple_equal(const Dst *lhs, const Dst *rhs);
int dst_tuple_compare(const Dst *lhs, const Dst *rhs);

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
const uint8_t *dst_description(Dst x);
const uint8_t *dst_to_string(Dst x);
const char *dst_to_zerostring(Dst x);
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
#define dst_struct_raw(t) ((int32_t *)(t) - 4)
#define dst_struct_length(t) (dst_struct_raw(t)[0])
#define dst_struct_capacity(t) (dst_struct_raw(t)[1])
#define dst_struct_hash(t) (dst_struct_raw(t)[2])
/* Do something with the 4th header slot - flags? */
DstKV *dst_struct_begin(int32_t count);
void dst_struct_put(DstKV *st, Dst key, Dst value);
const DstKV *dst_struct_end(DstKV *st);
Dst dst_struct_get(const DstKV *st, Dst key);
const DstKV *dst_struct_next(const DstKV *st, const DstKV *kv);
DstTable *dst_struct_to_table(const DstKV *st);
int dst_struct_equal(const DstKV *lhs, const DstKV *rhs);
int dst_struct_compare(const DstKV *lhs, const DstKV *rhs);
const DstKV *dst_struct_find(const DstKV *st, Dst key);

/* Table functions */
DstTable *dst_table(int32_t capacity);
DstTable *dst_table_init(DstTable *table, int32_t capacity);
void dst_table_deinit(DstTable *table);
Dst dst_table_get(DstTable *t, Dst key);
Dst dst_table_rawget(DstTable *t, Dst key);
Dst dst_table_remove(DstTable *t, Dst key);
void dst_table_put(DstTable *t, Dst key, Dst value);
const DstKV *dst_table_next(DstTable *t, const DstKV *kv);
const DstKV *dst_table_to_struct(DstTable *t);
void dst_table_merge_table(DstTable *table, DstTable *other);
void dst_table_merge_struct(DstTable *table, const DstKV *other);
DstKV *dst_table_find(DstTable *t, Dst key);

/* Fiber */
DstFiber *dst_fiber(DstFunction *callee, int32_t capacity);

/* Treat similar types through uniform interfaces for iteration */
int dst_seq_view(Dst seq, const Dst **data, int32_t *len);
int dst_chararray_view(Dst str, const uint8_t **data, int32_t *len);
int dst_hashtable_view(Dst tab, const DstKV **data, int32_t *len, int32_t *cap);

/* Abstract */
#define dst_abstract_header(u) ((DstAbstractHeader *)(u) - 1)
#define dst_abstract_type(u) (dst_abstract_header(u)->type)
#define dst_abstract_size(u) (dst_abstract_header(u)->size)
void *dst_abstract(const DstAbstractType *type, size_t size);

/* Native */
DstCFunction dst_native(const char *name, const uint8_t **error);

/* GC */
void dst_mark(Dst x);
void dst_sweep(void);
void dst_collect(void);
void dst_clear_memory(void);
void dst_gcroot(Dst root);
int dst_gcunroot(Dst root);
int dst_gcunrootall(Dst root);
#define dst_maybe_collect() do {\
    if (dst_vm_next_collection >= dst_vm_gc_interval) dst_collect(); } while (0)
#define dst_gclock() (dst_vm_gc_suspend++)
#define dst_gcunlock(lock) (dst_vm_gc_suspend = lock)

/* Functions */
DstFuncDef *dst_funcdef_alloc(void);
DstFunction *dst_thunk(DstFuncDef *def);
int dst_verify(DstFuncDef *def);
DstFunction *dst_quick_asm(int32_t arity, int varargs, int32_t slots, const uint32_t *bytecode, size_t bytecode_size);

/* Misc */
int dst_equals(Dst x, Dst y);
int32_t dst_hash(Dst x);
int dst_compare(Dst x, Dst y);
Dst dst_get(Dst ds, Dst key);
void dst_put(Dst ds, Dst key, Dst value);
const DstKV *dst_next(Dst ds, const DstKV *kv);
int32_t dst_length(Dst x);
Dst dst_getindex(Dst ds, int32_t index);
void dst_setindex(Dst ds, Dst value, int32_t index);
int dst_cstrcmp(const uint8_t *str, const char *other);

/* VM functions */
int dst_init(void);
void dst_deinit(void);
Dst dst_run(DstFiber *fiber);
Dst dst_resume(DstFiber *fiber, int32_t argn, const Dst *argv);

/* C Function helpers */
#define dst_throw(a, e) (*((a).ret) = dst_cstringv(e), 1)
#define dst_throwv(a, v) (*((a).ret) = (v), 1)
#define dst_return(a, v) (*((a).ret) = (v), 0)

/* Env helpers */
void dst_env_def(DstTable *env, const char *name, Dst val);
void dst_env_var(DstTable *env, const char *name, Dst val);
void dst_env_cfuns(DstTable *env, const DstReg *cfuns);
Dst dst_env_resolve(DstTable *env, const char *name);
DstTable *dst_env_arg(DstArgs args);

/* STL */
DstTable *dst_stl_env(void);

#ifdef __cplusplus
}
#endif

#endif /* DST_H_defined */
