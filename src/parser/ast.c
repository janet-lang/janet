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

#include <dst/dst.h>

/* Mark an ast node */
static int dst_ast_gcmark(void *p, size_t size) {
    DstAst *ast = (DstAst *)p;
    (void) size;
    dst_mark(ast->value);
    return 0;
}

/* AST type */
static DstAbstractType dst_ast_type = {
    ":parser.ast",
    NULL,
    dst_ast_gcmark
};

/* Create an ast type */
Dst dst_ast_wrap(Dst x, int32_t start, int32_t end) {
    DstAst *ast = dst_abstract(&dst_ast_type, sizeof(DstAst));
    ast->value = x;
    ast->source_start = start;
    ast->source_end = end;
    ast->flags = 1 << dst_type(x);
    return dst_wrap_abstract(ast);
}

/* Get the node associated with a value */
DstAst *dst_ast_node(Dst x) {
    if (dst_checktype(x, DST_ABSTRACT) &&
            dst_abstract_type(dst_unwrap_abstract(x)) == &dst_ast_type) {
        DstAst *ast = (DstAst *)dst_unwrap_abstract(x);
        return ast;
    }
    return NULL;
}

/* Unwrap an ast value one level deep */
Dst dst_ast_unwrap1(Dst x) {
    if (dst_checktype(x, DST_ABSTRACT) &&
            dst_abstract_type(dst_unwrap_abstract(x)) == &dst_ast_type) {
        DstAst *ast = (DstAst *)dst_unwrap_abstract(x);
        return ast->value;
    }
    return x;
}

Dst dst_ast_unwrap(Dst x);

static Dst astunwrap_array(DstArray *other) {
    DstArray *array;
    Dst diffval;
    int32_t i, prescan;
    for (prescan = 0; prescan < other->count; prescan++) {
        diffval = dst_ast_unwrap(other->data[prescan]);
        if (!dst_equals(diffval, other->data[prescan])) break;
    }
    if (prescan == other->count) return dst_wrap_array(other);
    array = dst_array(other->count);
    for (i = 0; i < prescan; i++) {
        array->data[i] = other->data[i];
    }
    array->data[prescan] = diffval;
    for (i = prescan + 1; i < other->count; i++) {
        array->data[i] = dst_ast_unwrap(other->data[i]);
    }
    array->count = other->count;
    return dst_wrap_array(array);
}

static Dst astunwrap_tuple(const Dst *other) {
    Dst *tuple;
    int32_t i, prescan;
    Dst diffval;
    for (prescan = 0; prescan < dst_tuple_length(other); prescan++) {
        diffval = dst_ast_unwrap(other[prescan]);
        if (!dst_equals(diffval, other[prescan])) break;
    }
    if (prescan == dst_tuple_length(other)) return dst_wrap_tuple(other);
    tuple = dst_tuple_begin(dst_tuple_length(other));
    for (i = 0; i < prescan; i++) {
        tuple[i] = other[i];
    }
    tuple[prescan] = diffval;
    for (i = prescan + 1; i < dst_tuple_length(other); i++) {
        tuple[i] = dst_ast_unwrap(other[i]);
    }
    return dst_wrap_tuple(dst_tuple_end(tuple));
}

static Dst astunwrap_struct(const DstKV *other) {
    DstKV *st;
    const DstKV *prescan, *iter;
    Dst diffval, diffkey;
    prescan = NULL;
    while ((prescan = dst_struct_next(other, prescan))) {
        diffkey = dst_ast_unwrap(prescan->key);
        diffval = dst_ast_unwrap(prescan->value);
        if (!dst_equals(diffkey, prescan->key) ||
            !dst_equals(diffval, prescan->value))
            break;
    }
    if (!prescan) return dst_wrap_struct(other);
    st = dst_struct_begin(dst_struct_length(other));
    iter = NULL;
    while ((iter = dst_struct_next(other, iter))) {
        if (iter == prescan) break;
        dst_struct_put(st, iter->key, iter->value);
    }
    dst_struct_put(st, diffkey, diffval);
    while ((iter = dst_struct_next(other, iter))) {
        dst_struct_put(st, 
                dst_ast_unwrap(iter->key),
                dst_ast_unwrap(iter->value));
    }
    return dst_wrap_struct(dst_struct_end(st));
}

static Dst astunwrap_table(DstTable *other) {
    DstTable *table;
    const DstKV *prescan, *iter;
    Dst diffval, diffkey;
    prescan = NULL;
    while ((prescan = dst_table_next(other, prescan))) {
        diffkey = dst_ast_unwrap(prescan->key);
        diffval = dst_ast_unwrap(prescan->value);
        if (!dst_equals(diffkey, prescan->key) ||
            !dst_equals(diffval, prescan->value))
            break;
    }
    if (!prescan) return dst_wrap_table(other);
    table = dst_table(other->capacity);
    table->proto = other->proto;
    iter = NULL;
    while ((iter = dst_table_next(other, iter))) {
        if (iter == prescan) break;
        dst_table_put(table, iter->key, iter->value);
    }
    dst_table_put(table, diffkey, diffval);
    while ((iter = dst_table_next(other, iter))) {
        dst_table_put(table, 
                dst_ast_unwrap(iter->key),
                dst_ast_unwrap(iter->value));
    }
    return dst_wrap_table(table);
}

/* Unwrap an ast value recursively. Preserve as much structure as possible
 * to avoid unecessary allocation. */
Dst dst_ast_unwrap(Dst x) {
    x = dst_ast_unwrap1(x);
    switch (dst_type(x)) {
        default:
            return x;
        case DST_ARRAY:
            return astunwrap_array(dst_unwrap_array(x));
        case DST_TUPLE:
            return astunwrap_tuple(dst_unwrap_tuple(x));
        case DST_STRUCT:
            return astunwrap_struct(dst_unwrap_struct(x));
        case DST_TABLE:
            return astunwrap_table(dst_unwrap_table(x));
    }
}
