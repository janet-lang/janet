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

#include <gst/gst.h>

/**
 * Data format
 *
 * Types:
 *
 * Byte 0 to 200: small integer with value (byte - 100)
 * Byte 201: Nil
 * Byte 202: True
 * Byte 203: False
 * Byte 204: Number  - double format
 * Byte 205: String  - [u32 length]*[u8... characters]
 * Byte 206: Struct  - [u32 length]*2*[value... kvs]
 * Byte 207: Buffer  - [u32 capacity][u32 length]*[u8... characters]
 * Byte 208: Array   - [u32 length]*[value... elements]
 * Byte 209: Tuple   - [u32 length]*[value... elements]
 * Byte 210: Thread  - [value parent][value errorParent][u8 state][u32 frames]*[[value callee][value env]
 *  [u32 pcoffset][u32 ret][u32 args][u32 size]*[value ...stack]]
 * Byte 211: Table   - [u32 length]*2*[value... kvs]
 * Byte 212: FuncDef - [u32 locals][u32 arity][u32 flags][u32 literallen]*[value...
 *  literals][u32 bytecodelen]*[u16... bytecode]
 * Byte 213: FuncEnv - [value thread][u32 length]*[value ...upvalues]
 *  (upvalues is not read if thread is a thread object)
 * Byte 214: Func    - [value parent][value def][value env]
 *  (nil values indicate empty)
 * Byte 215: LUdata  - [value meta][u32 length]*[u8... bytes]
 * Byte 216: CFunc   - [u32 length]*[u8... idstring]
 * Byte 217: Ref     - [u32 id]
 * Byte 218: Integer - [i64 value]
 * Byte 219: Symbol  - [u32 length]*[u8... characters]
 */

/* Error at buffer end */
static const char UEB[] = "unexpected end of buffer";

/* Read 4 bytes as an unsigned integer */
static uint32_t bytes2u32(const uint8_t *bytes) {
    union {
        uint8_t bytes[4];
        uint32_t u32;
    } u;
    gst_memcpy(u.bytes, bytes, 4 * sizeof(uint8_t));
    return u.u32;
}

/* Read 2 bytes as unsigned short */
static uint16_t bytes2u16(const uint8_t *bytes) {
    union {
        uint8_t bytes[2];
        uint16_t u16;
    } u;
    gst_memcpy(u.bytes, bytes, 2 * sizeof(uint8_t));
    return u.u16;
}

/* Read 8 bytes as a double */
static double bytes2dbl(const uint8_t *bytes) {
    union {
        uint8_t bytes[8];
        double dbl;
    } u;
    gst_memcpy(u.bytes, bytes, 8 * sizeof(uint8_t));
    return u.dbl;
}

/* Read 8 bytes as a integer */
static int64_t bytes2int(const uint8_t *bytes) {
    union {
        uint8_t bytes[8];
        int64_t i;
    } u;
    gst_memcpy(u.bytes, bytes, 8 * sizeof(uint8_t));
    return u.i;
}

/* Read a string and turn it into a gst value. Returns
 * an error message if there is an error message during
 * deserialization. If successful, the resulting value is
 * passed by reference. */
static const char *gst_deserialize_impl(
        Gst *vm,
        const uint8_t *data,
        const uint8_t *end,
        const uint8_t **newData,
        GstArray *visited,
        GstValue *out,
        int depth) {

    GstValue ret;
    ret.type = GST_NIL;
    GstValue *buffer;
    uint32_t length, i;
    const char *err;

    /* Handle errors as needed */
#define deser_error(e) return (e)

    /* Assertions */
#define deser_assert(c, e) do{if(!(c))deser_error(e);}while(0)

    /* Assert enough buffer */
#define deser_datacheck(len) do{if (end < (data + len)) deser_error(UEB);}while(0)

    /* Check for enough space to read uint32_t */
#define read_u32(out) do{deser_datacheck(4); (out)=bytes2u32(data); data += 4; }while(0)
#define read_u16(out) do{deser_datacheck(2); (out)=bytes2u16(data); data += 2; }while(0)
#define read_dbl(out) do{deser_datacheck(8); (out)=bytes2dbl(data); data += 8; }while(0)
#define read_i64(out) do{deser_datacheck(8); (out)=bytes2int(data); data += 8; }while(0)

    /* Check if we have recursed too deeply */
    if (depth++ > GST_RECURSION_GUARD) {
        return "deserialize recursed too deeply";
    }

    /* Check enough buffer left to read one byte */
    if (data >= end) deser_error(UEB);

    /* Small integer */
    if (*data < 201) {
        ret.type = GST_INTEGER;
        ret.data.integer = *data - 100;
        *newData = data + 1;
        *out = ret;
        return NULL;
    }

    /* Main switch for types */
    switch (*data++) {

        default:
            return "unable to deserialize";

        case 201: /* Nil */
            ret.type = GST_NIL;
            break;

        case 202: /* True */
            ret.type = GST_BOOLEAN;
            ret.data.boolean = 1;
            break;

        case 203: /* False */
            ret.type = GST_BOOLEAN;
            ret.data.boolean = 0;
            break;

        case 204: /* Long number (double) */
            ret.type = GST_REAL;
            read_dbl(ret.data.real);
            break;

        case 205: /* String */
        case 219: /* Symbol */
            ret.type = data[-1] == 205 ? GST_STRING : GST_SYMBOL;
            read_u32(length);
            deser_datacheck(length);
            ret.data.string = gst_string_b(vm, data, length);
            data += length;
            gst_array_push(vm, visited, ret);
            break;

        case 206: /* Struct */
            ret.type = GST_STRUCT;
            read_u32(length);
            buffer = gst_struct_begin(vm, length);
            for (i = 0; i < length; ++i) {
                GstValue k, v;
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, &k, depth)))
                    return err;
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, &v, depth)))
                    return err;
                gst_struct_put(buffer, k, v);
            }
            ret.data.st = gst_struct_end(vm, buffer);
            gst_array_push(vm, visited, ret);
            break;

        case 207: /* Buffer */
            {
                uint32_t cap;
                ret.type = GST_BYTEBUFFER;
                read_u32(cap);
                read_u32(length);
                deser_datacheck(length);
                ret.data.buffer = gst_alloc(vm, sizeof(GstBuffer));
                ret.data.buffer->data = gst_alloc(vm, cap);
                gst_memcpy(ret.data.buffer->data, data, length);
                ret.data.buffer->count = length;
                ret.data.buffer->capacity = cap;
                gst_array_push(vm, visited, ret);
            }
            break;

        case 208: /* Array */
            ret.type = GST_ARRAY;
            read_u32(length);
            buffer = gst_alloc(vm, length * sizeof(GstValue));
            ret.data.array = gst_alloc(vm, sizeof(GstArray));
            ret.data.array->data = buffer;
            ret.data.array->count = length;
            ret.data.array->capacity = length;
            gst_array_push(vm, visited, ret);
            for (i = 0; i < length; ++i)
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, buffer + i, depth)))
                    return err;
            break;

        case 209: /* Tuple */
            ret.type = GST_TUPLE;
            read_u32(length);
            buffer = gst_tuple_begin(vm, length);
            for (i = 0; i < length; ++i)
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, buffer + i, depth)))
                    return err;
            ret.type = GST_TUPLE;
            ret.data.tuple = gst_tuple_end(vm, buffer);
            gst_array_push(vm, visited, ret);
            break;

        case 210: /* Thread */
            {
                GstThread *t;
                GstValue *stack;
                uint16_t prevsize = 0;
                uint8_t statusbyte;
                t = gst_thread(vm, gst_wrap_nil(), 64);
                ret = gst_wrap_thread(t);
                gst_array_push(vm, visited, ret);
                err = gst_deserialize_impl(vm, data, end, &data, visited, &ret, depth);
                if (err != NULL) return err;
                if (ret.type == GST_NIL) {
                    t->parent = NULL;
                } else if (ret.type == GST_THREAD) {
                    t->parent = ret.data.thread;
                } else {
                    return "expected thread parent to be thread";
                }
                err = gst_deserialize_impl(vm, data, end, &data, visited, &ret, depth);
                if (err != NULL) return err;
                if (ret.type == GST_NIL) {
                    t->errorParent = NULL;
                } else if (ret.type == GST_THREAD) {
                    t->errorParent = ret.data.thread;
                } else {
                    return "expected thread error parent to be thread";
                }
                deser_assert(data < end, UEB);
                statusbyte = *data++;
                read_u32(length);
                /* Set status */
                t->status = statusbyte % 4;
                /* Add frames */
                for (i = 0; i < length; ++i) {
                    GstValue callee, env;
                    uint32_t pcoffset;
                    uint16_t ret, args, size, j;
                    /* Read the stack */
                    err = gst_deserialize_impl(vm, data, end, &data, visited, &callee, depth);
                    if (err != NULL) return err;
                    err = gst_deserialize_impl(vm, data, end, &data, visited, &env, depth);
                    if (err != NULL) return err;
                    if (env.type != GST_FUNCENV && env.type != GST_NIL)
                        return "expected funcenv in stackframe";
                    /* Create a new frame */
                    if (i > 0)
                        gst_thread_beginframe(vm, t, gst_wrap_nil(), 0);
                    read_u32(pcoffset);
                    read_u32(ret);
                    read_u32(args);
                    read_u32(size);
                    /* Set up the stack */
                    stack = gst_thread_stack(t);
                    if (callee.type == GST_FUNCTION) {
                        gst_frame_pc(stack) = callee.data.function->def->byteCode + pcoffset;
                    }
                    gst_frame_ret(stack) = ret;
                    gst_frame_args(stack) = args;
                    gst_frame_size(stack) = size;
                    gst_frame_prevsize(stack) = prevsize;
                    gst_frame_callee(stack) = callee;
                    if (env.type == GST_NIL)
                        gst_frame_env(stack) = NULL;
                    else
                        gst_frame_env(stack) = env.data.env;
                    prevsize = size;
                    /* Push stack args */
                    for (j = 0; j < size; ++j) {
                        GstValue temp;
                        err = gst_deserialize_impl(vm, data, end, &data, visited, &temp, depth);
                        gst_thread_push(vm, t, temp);
                    }
                }
            }
            break;

        case 211: /* Table */
            read_u32(length);
            ret = gst_wrap_table(gst_table(vm, 2 * length));
            gst_array_push(vm, visited, ret);
            for (i = 0; i < length; ++i) {
                GstValue key, value;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &key, depth);
                if (err != NULL) return err;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &value, depth);
                if (err != NULL) return err;
                gst_table_put(vm, ret.data.table, key, value);
            }
            break;

        case 212: /* Funcdef */
            {
                GstFuncDef *def;
                uint32_t locals, arity, literalsLen, byteCodeLen, flags;
                read_u32(locals);
                read_u32(arity);
                read_u32(flags);
                read_u32(literalsLen);
                def = gst_alloc(vm, sizeof(GstFuncDef));
                ret = gst_wrap_funcdef(def);
                gst_array_push(vm, visited, ret);
                def->locals = locals;
                def->arity = arity;
                def->flags = flags;
                def->literalsLen = literalsLen;
                if (literalsLen > 0) {
                    def->literals = gst_alloc(vm, literalsLen * sizeof(GstValue));
                } else {
                    def->literals = NULL;
                }
                for (i = 0; i < literalsLen; ++i) {
                    err = gst_deserialize_impl(vm, data, end, &data, visited, def->literals + i, depth);
                    if (err != NULL) return err;
                }
                read_u32(byteCodeLen);
                deser_datacheck(byteCodeLen * sizeof(uint16_t));
                def->byteCode = gst_alloc(vm, byteCodeLen * sizeof(uint16_t));
                def->byteCodeLen = byteCodeLen;
                for (i = 0; i < byteCodeLen; ++i) {
                    read_u16(def->byteCode[i]);
                }
            }
            break;

        case 213: /* Funcenv */
            {
                GstValue thread;
                ret.type = GST_FUNCENV;
                ret.data.env = gst_alloc(vm, sizeof(GstFuncEnv));
                gst_array_push(vm, visited, ret);
                err = gst_deserialize_impl(vm, data, end, &data, visited, &thread, depth);
                if (err != NULL) return err;
                read_u32(length);
                ret.data.env->stackOffset = length;
                if (thread.type == GST_THREAD) {
                    ret.data.env->thread = thread.data.thread;
                } else {
                    ret.data.env->thread = NULL;
                    ret.data.env->values = gst_alloc(vm, sizeof(GstValue) * length);
                    for (i = 0; i < length; ++i) {
                        GstValue item;
                        err = gst_deserialize_impl(vm, data, end, &data, visited, &item, depth);
                        if (err != NULL) return err;
                        ret.data.env->values[i] = item;
                    }
                }
            }
            break;

        case 214: /* Function */
            {
                GstValue parent, def, env;
                ret.type = GST_FUNCTION;
                ret.data.function = gst_alloc(vm, sizeof(GstFunction));
                gst_array_push(vm, visited, ret);
                err = gst_deserialize_impl(vm, data, end, &data, visited, &parent, depth);
                if (err != NULL) return err;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &env, depth);
                if (err != NULL) return err;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &def, depth);
                if (err != NULL) return err;
                if (parent.type == GST_NIL) {
                    ret.data.function->parent = NULL;
                } else if (parent.type == GST_FUNCTION) {
                    ret.data.function->parent = parent.data.function;
                } else {
                    deser_error("expected function");
                }
                deser_assert(def.type == GST_FUNCDEF, "expected funcdef");
                ret.data.function->def = def.data.def;
                if (env.type == GST_NIL) {
                    ret.data.function->env = NULL;
                } else {
                    deser_assert(env.type == GST_FUNCENV, "expected funcenv");
                    ret.data.function->env = env.data.env;
                }
            }
            break;

        case 215: /* LUdata */
            {
                /* TODO enable deserialization of userdata through registration
                 * to names in vm. */
            }
            break;

        case 216: /* C function */
            {
                GstValue id;
                read_u32(length);
                deser_datacheck(length);
                id = gst_wrap_string(gst_string_b(vm, data, length));
                data += length;
                ret = gst_table_get(vm->registry, id);
                if (ret.type != GST_CFUNCTION) {
                    deser_error("unabled to deserialize c function");
                }
                break;
            }
            break;

        case 217: /* Reference */
            read_u32(length);
            deser_assert(visited->count > length, "invalid reference");
            ret = visited->data[length];
            break;

        case 218: /* Integer */
            ret.type = GST_INTEGER;
            read_i64(ret.data.integer);
            break;
    }
    *out = ret;
    *newData = data;
    return NULL;
}

/* Load a value from data */
const char *gst_deserialize(
        Gst *vm,
        const uint8_t *data,
        uint32_t len,
        GstValue *out,
        const uint8_t **nextData) {
    GstValue ret;
    const char *err;
    GstArray *visited = gst_array(vm, 10);
    err = gst_deserialize_impl(vm, data, data + len, nextData, visited, &ret, 0);
    if (err != NULL) return err;
    *out = ret;
    return NULL;
}

/* Allow appending other types to buffers */
BUFFER_DEFINE(real, GstReal)
BUFFER_DEFINE(integer, GstInteger)
BUFFER_DEFINE(u32, uint32_t)
BUFFER_DEFINE(u16, uint16_t)

/* Serialize a value and write to a buffer. Returns possible
 * error messages. */
static const char *gst_serialize_impl(
        Gst *vm,
        GstBuffer *buffer,
        GstTable *visited,
        uint32_t *nextId,
        GstValue x,
        int depth) {

    uint32_t i, count;
    const char *err;
    GstValue check;

#define write_byte(b) gst_buffer_push(vm, buffer, (b))
#define write_u32(b) gst_buffer_push_u32(vm, buffer, (b))
#define write_u16(b) gst_buffer_push_u16(vm, buffer, (b))
#define write_dbl(b) gst_buffer_push_real(vm, buffer, (b))
#define write_int(b) gst_buffer_push_integer(vm, buffer, (b))
    /*printf("Type: %d\n", x.type);*/

    /* Check if we have gone too deep */
    if (depth++ > GST_RECURSION_GUARD) {
        return "serialize recursed too deeply";
    }

    /* Check non reference types - if successful, return NULL */
    switch (x.type) {
        case GST_USERDATA:
        case GST_NIL:
            write_byte(201);
            return NULL;
        case GST_BOOLEAN:
            write_byte(x.data.boolean ? 202 : 203);
            return NULL;
        case GST_REAL:
            write_byte(204);
            write_dbl(x.data.real);
            return NULL;
        case GST_INTEGER:
            if (x.data.integer <= 100 && x.data.integer >= -100) {
                write_byte(x.data.integer + 100);
            } else {
                write_byte(218);
                write_int(x.data.integer);
            }
            return NULL;
        default:
            break;
    }

    /* Check if already seen - if so, use reference */
    check = gst_table_get(visited, x);
    if (check.type == GST_INTEGER) {
        write_byte(217);
        write_u32((uint32_t) check.data.integer);
        return NULL;
    }

    /* Check tuples and structs before other reference types.
     * They are immutable, and thus cannot be referenced by other values
     * until they are fully constructed. This creates some strange behavior
     * if they are treated like other reference types because they cannot
     * be added to the visited table before recursing into serializing their
     * arguments */
    if (x.type == GST_STRUCT || x.type == GST_TUPLE) {
        if (x.type == GST_STRUCT) {
            const GstValue *data;
            write_byte(206);
            gst_hashtable_view(x, &data, &count);
            write_u32(gst_struct_length(x.data.st));
            for (i = 0; i < count; i += 2) {
                if (data[i].type != GST_NIL) {
                    err = gst_serialize_impl(vm, buffer, visited, nextId, data[i], depth);
                    if (err != NULL) return err;
                    err = gst_serialize_impl(vm, buffer, visited, nextId, data[i + 1], depth);
                    if (err != NULL) return err;
                }
            }
        } else {
            write_byte(209);
            count = gst_tuple_length(x.data.tuple);
            write_u32(count);
            for (i = 0; i < count; ++i) {
                err = gst_serialize_impl(vm, buffer, visited, nextId, x.data.tuple[i], depth);
                if (err != NULL) return err;
            }
        }
        /* Record reference after serialization */
        gst_table_put(vm, visited, x, gst_wrap_integer(*nextId));
        *nextId = *nextId + 1;
        return NULL;
    }

    /* Record reference before serialization */
    gst_table_put(vm, visited, x, gst_wrap_integer(*nextId));
    *nextId = *nextId + 1;

    /* Check reference types */
    switch (x.type) {
        default:
            return "unable to serialize type";

        case GST_STRING:
        case GST_SYMBOL:
            write_byte(x.type == GST_STRING ? 205 : 219);
            count = gst_string_length(x.data.string);
            write_u32(count);
            for (i = 0; i < count; ++i) {
                write_byte(x.data.string[i]);
            }
            break;

        case GST_CFUNCTION:
            write_byte(216);
            {
                GstValue id = gst_table_get(vm->registry, x);
                count = gst_string_length(id.data.string);
                write_u32(count);
                for (i = 0; i < count; ++i) {
                    write_byte(id.data.string[i]);
                }
            }
            break;

        case GST_TABLE:
            {
                const GstValue *data;
                write_byte(211);
                gst_hashtable_view(x, &data, &count);
                write_u32(x.data.table->count);
                for (i = 0; i < count; i += 2) {
                    if (data[i].type != GST_NIL) {
                        err = gst_serialize_impl(vm, buffer, visited, nextId, data[i], depth);
                        if (err != NULL) return err;
                        err = gst_serialize_impl(vm, buffer, visited, nextId, data[i + 1], depth);
                        if (err != NULL) return err;
                    }
                }
            }
            break;

        case GST_BYTEBUFFER:
            write_byte(207);
            count = x.data.buffer->count;
            write_u32(x.data.buffer->capacity);
            write_u32(count);
            for (i = 0; i < count; ++i) {
                write_byte(x.data.buffer->data[i]);
            }
            break;

        case GST_ARRAY:
            write_byte(208);
            count = x.data.array->count;
            write_u32(count);
            for (i = 0; i < count; ++i) {
                err = gst_serialize_impl(vm, buffer, visited, nextId, x.data.array->data[i], depth);
                if (err != NULL) return err;
            }
            break;

        case GST_THREAD:
            {
                GstThread *t = x.data.thread;
                const GstValue *stack = t->data + GST_FRAME_SIZE;
                uint32_t framecount = gst_thread_countframes(t);
                uint32_t i;
                write_byte(210);
                if (t->parent)
                    err = gst_serialize_impl(vm, buffer, visited, nextId, gst_wrap_thread(t->parent), depth);
                else
                    err = gst_serialize_impl(vm, buffer, visited, nextId, gst_wrap_nil(), depth);
                if (t->errorParent)
                    err = gst_serialize_impl(vm, buffer, visited, nextId,
							gst_wrap_thread(t->errorParent), depth);
                else
                    err = gst_serialize_impl(vm, buffer, visited, nextId, gst_wrap_nil(), depth);
                if (err != NULL) return err;
                /* Write the status byte */
                write_byte(t->status);
                /* Write number of stack frames */
                write_u32(framecount);
                /* Write stack frames */
                for (i = 0; i < framecount; ++i) {
                    uint32_t j, size;
                    GstValue callee = gst_frame_callee(stack);
                    GstFuncEnv *env = gst_frame_env(stack);
                    err = gst_serialize_impl(vm, buffer, visited, nextId, callee, depth);
                    if (err != NULL) return err;
                    if (env)
                        err = gst_serialize_impl(vm, buffer, visited, nextId, gst_wrap_funcenv(env), depth);
                    else
                        err = gst_serialize_impl(vm, buffer, visited, nextId, gst_wrap_nil(), depth);
                    if (err != NULL) return err;
                    if (callee.type == GST_FUNCTION) {
                        write_u32(gst_frame_pc(stack) - callee.data.function->def->byteCode);
                    } else {
                        write_u32(0);
                    }
                    write_u32(gst_frame_ret(stack));
                    write_u32(gst_frame_args(stack));
                    size = gst_frame_size(stack);
                    write_u32(size);
                    for (j = 0; j < size; ++j) {
                        err = gst_serialize_impl(vm, buffer, visited, nextId, stack[j], depth);
                        if (err != NULL) return err;
                    }
                    /* Next stack frame */
                    stack += size + GST_FRAME_SIZE;
                }
            }
            break;

        case GST_FUNCDEF: /* Funcdef */
            {
                GstFuncDef *def = x.data.def;
                write_byte(212);
                write_u32(def->locals);
                write_u32(def->arity);
                write_u32(def->flags);
                write_u32(def->literalsLen);
                for (i = 0; i < def->literalsLen; ++i) {
                    err = gst_serialize_impl(vm, buffer, visited, nextId, def->literals[i], depth);
                    if (err != NULL) return err;
                }
                write_u32(def->byteCodeLen);
                for (i = 0; i < def->byteCodeLen; ++i) {
                    write_u16(def->byteCode[i]);
                }
            }
            break;

        case GST_FUNCENV: /* Funcenv */
            {
                GstFuncEnv *env = x.data.env;
                write_byte(213);
                if (env->thread) {
                    err = gst_serialize_impl(vm, buffer, visited, nextId, gst_wrap_thread(env->thread), depth);
                    if (err != NULL) return err;
                    write_u32(env->stackOffset);
                } else {
                    write_byte(201); /* Write nil */
                    write_u32(env->stackOffset);
                    for (i = 0; i < env->stackOffset; ++i) {
                        err = gst_serialize_impl(vm, buffer, visited, nextId, env->values[i], depth);
                        if (err != NULL) return err;
                    }
                }
            }
            break;

        case GST_FUNCTION: /* Function */
            {
                GstValue pv, ev, dv;
                GstFunction *fn = x.data.function;
                write_byte(214);
                pv = fn->parent ? gst_wrap_function(fn->parent) : gst_wrap_nil();
                dv = gst_wrap_funcdef(fn->def);
                ev = fn->env ? gst_wrap_funcenv(fn->env) : gst_wrap_nil();
                err = gst_serialize_impl(vm, buffer, visited, nextId, pv, depth);
                if (err != NULL) return err;
                err = gst_serialize_impl(vm, buffer, visited, nextId, ev, depth);
                if (err != NULL) return err;
                err = gst_serialize_impl(vm, buffer, visited, nextId, dv, depth);
                if (err != NULL) return err;
            }
            break;
    }

    /* Return success */
    return NULL;
}

/* Serialize an object */
const char *gst_serialize(Gst *vm, GstBuffer *buffer, GstValue x) {
    uint32_t nextId = 0;
    uint32_t oldCount = buffer->count;
    const char *err;
    GstTable *visited = gst_table(vm, 10);
    err = gst_serialize_impl(vm, buffer, visited, &nextId, x, 0);
    if (err != NULL) {
        buffer->count = oldCount;
    }
    return err;
}
