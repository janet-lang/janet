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

/* Number scanning */
Dst dst_scan_number(const uint8_t *src, int32_t len);
int32_t dst_scan_integer(const uint8_t *str, int32_t len, int *err);
double dst_scan_real(const uint8_t *str, int32_t len, int *err);

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
void dst_buffer_setcount(DstBuffer *buffer, int32_t count);
int dst_buffer_extra(DstBuffer *buffer, int32_t n);
int dst_buffer_push_bytes(DstBuffer *buffer, const uint8_t *string, int32_t len);
int dst_buffer_push_string(DstBuffer *buffer, const uint8_t *string);
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
int dst_gclock();
void dst_gcunlock(int handle);

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

/* Env helpers */
void dst_env_def(DstTable *env, const char *name, Dst val);
void dst_env_var(DstTable *env, const char *name, Dst val);
void dst_env_cfuns(DstTable *env, const DstReg *cfuns);
Dst dst_env_resolve(DstTable *env, const char *name);
DstTable *dst_env_arg(DstArgs args);

/* STL */
DstTable *dst_stl_env(void);

/* C Function helpers */
int dst_arity_err(DstArgs args, int32_t n, const char *prefix);
int dst_type_err(DstArgs args, int32_t n, DstType expected);
int dst_typemany_err(DstArgs args, int32_t n, int expected);
int dst_typeabstract_err(DstArgs args, int32_t n, const DstAbstractType *at);

/* Macros */
#define DST_THROW(a, e) return (*((a).ret) = dst_cstringv(e), 1)
#define DST_THROWV(a, v) return (*((a).ret) = (v), 1)
#define DST_RETURN(a, v) return (*((a).ret) = (v), 0)

/* Early exit macros */
#define DST_MAXARITY(A, N) do { if ((A).n > (N))\
    return dst_arity_err(A, N, "at most "); } while (0)
#define DST_MINARITY(A, N) do { if ((A).n < (N))\
    return dst_arity_err(A, N, "at least "); } while (0)
#define DST_FIXARITY(A, N) do { if ((A).n != (N))\
    return dst_arity_err(A, N, ""); } while (0)
#define DST_CHECK(A, N, T) do {\
    if ((A).n > (N)) {\
       if (!dst_checktype((A).v[(N)], (T))) return dst_type_err(A, N, T);\
    } else {\
       if ((T) != DST_NIL) return dst_type_err(A, N, T);\
    }\
} while (0)
#define DST_CHECKMANY(A, N, TS) do {\
    if ((A).n > (N)) {\
        DstType t = dst_type((A).v[(N)]);\
        if (!((1 << t) & (TS))) return dst_typemany_err(A, N, TS);\
    } else {\
       if (!((TS) & DST_NIL)) return dst_typemany_err(A, N, TS);\
    }\
} while (0)

#define DST_CHECKABSTRACT(A, N, AT) do {\
    if ((A).n > (N)) {\
        Dst x = (A).v[(N)];\
        if (!dst_checktype(x, DST_ABSTRACT) ||\
                dst_abstract_type(dst_unwrap_abstract(x)) != (AT))\
        return dst_typeabstract_err(A, N, AT);\
    } else {\
        return dst_typeabstract_err(A, N, AT);\
    }\
} while (0)

#define DST_ARG_NUMBER(DEST, A, N) do { \
    if ((A).n <= (N)) \
        return dst_typemany_err(A, N, DST_TFLAG_NUMBER);\
    Dst val = (A).v[(N)];\
    if (dst_checktype(val, DST_REAL)) { \
        DEST = dst_unwrap_real(val); \
    } else if (dst_checktype(val, DST_INTEGER)) {\
        DEST = (double) dst_unwrap_integer(val);\
    }\
    else return dst_typemany_err(A, N, DST_TFLAG_NUMBER); \
} while (0)

#define DST_ARG_BOOLEAN(DEST, A, N) do { \
    DST_CHECKMANY(A, N, DST_TFLAG_TRUE | DST_TFLAG_FALSE);\
    DEST = dst_unwrap_boolean((A).v[(N)]); \
} while (0)

#define DST_ARG_BYTES(DESTBYTES, DESTLEN, A, N) do {\
    if ((A).n <= (N)) return dst_typemany_err(A, N, DST_TFLAG_BYTES);\
    if (!dst_chararray_view((A).v[(N)], &(DESTBYTES), &(DESTLEN))) {\
        return dst_typemany_err(A, N, DST_TFLAG_BYTES);\
    }\
} while (0)

#define _DST_ARG(TYPE, NAME, DEST, A, N) do { \
    DST_CHECK(A, N, TYPE);\
    DEST = dst_unwrap_##NAME((A).v[(N)]); \
} while (0)

#define DST_ARG_FIBER(DEST, A, N) _DST_ARG(DST_FIBER, fiber, DEST, A, N)
#define DST_ARG_INTEGER(DEST, A, N) _DST_ARG(DST_INTEGER, integer, DEST, A, N)
#define DST_ARG_REAL(DEST, A, N) _DST_ARG(DST_REAL, real, DEST, A, N)
#define DST_ARG_STRING(DEST, A, N) _DST_ARG(DST_STRING, string, DEST, A, N)
#define DST_ARG_SYMBOL(DEST, A, N) _DST_ARG(DST_SYMBOL, symbol, DEST, A, N)
#define DST_ARG_ARRAY(DEST, A, N) _DST_ARG(DST_ARRAY, array, DEST, A, N)
#define DST_ARG_TUPLE(DEST, A, N) _DST_ARG(DST_TUPLE, tuple, DEST, A, N)
#define DST_ARG_TABLE(DEST, A, N) _DST_ARG(DST_TABLE, table, DEST, A, N)
#define DST_ARG_STRUCT(DEST, A, N) _DST_ARG(DST_STRUCT, st, DEST, A, N)
#define DST_ARG_BUFFER(DEST, A, N) _DST_ARG(DST_BUFFER, buffer, DEST, A, N)
#define DST_ARG_FUNCTION(DEST, A, N) _DST_ARG(DST_FUNCTION, function, DEST, A, N)
#define DST_ARG_CFUNCTION(DEST, A, N) _DST_ARG(DST_CFUNCTION, cfunction, DEST, A, N)
#define DST_ARG_ABSTRACT(DEST, A, N) _DST_ARG(DST_ABSTRACT, abstract, DEST, A, N)

#define DST_RETURN_NIL(A) return 0
#define DST_RETURN_FALSE(A) DST_RETURN(A, dst_wrap_false())
#define DST_RETURN_TRUE(A) DST_RETURN(A, dst_wrap_true())
#define DST_RETURN_BOOLEAN(A, X) DST_RETURN(A, dst_wrap_boolean(X))
#define DST_RETURN_FIBER(A, X) DST_RETURN(A, dst_wrap_fiber(X))
#define DST_RETURN_INTEGER(A, X) DST_RETURN(A, dst_wrap_integer(X))
#define DST_RETURN_REAL(A, X) DST_RETURN(A, dst_wrap_real(X))
#define DST_RETURN_STRING(A, X) DST_RETURN(A, dst_wrap_string(X))
#define DST_RETURN_SYMBOL(A, X) DST_RETURN(A, dst_wrap_symbol(X))
#define DST_RETURN_ARRAY(A, X) DST_RETURN(A, dst_wrap_array(X))
#define DST_RETURN_TUPLE(A, X) DST_RETURN(A, dst_wrap_tuple(X))
#define DST_RETURN_TABLE(A, X) DST_RETURN(A, dst_wrap_table(X))
#define DST_RETURN_STRUCT(A, X) DST_RETURN(A, dst_wrap_struct(X))
#define DST_RETURN_BUFFER(A, X) DST_RETURN(A, dst_wrap_buffer(X))
#define DST_RETURN_FUNCTION(A, X) DST_RETURN(A, dst_wrap_function(X))
#define DST_RETURN_CFUNCTION(A, X) DST_RETURN(A, dst_wrap_cfunction(X))
#define DST_RETURN_ABSTRACT(A, X) DST_RETURN(A, dst_wrap_abstract(X))

#define DST_RETURN_CSTRING(A, X) DST_RETURN(A, dst_cstringv(X))
#define DST_RETURN_CSYMBOL(A, X) DST_RETURN(A, dst_csymbolv(X))

#ifdef __cplusplus
}
#endif

#endif /* DST_H_defined */
