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
 * State is encoded as a string of unsigned bytes.
 *
 * Types:
 *
 * Byte 0 to 200: small integer byte - 100
 * Byte 201: Nil
 * Byte 202: True
 * Byte 203: False
 * Byte 204: Number  - double format
 * Byte 205: String  - [u32 length]*[u8... characters]
 * Byte 206: Symbol  - [u32 length]*[u8... characters]
 * Byte 207: Buffer  - [u32 length]*[u8... characters]
 * Byte 208: Array   - [u32 length]*[value... elements]
 * Byte 209: Tuple   - [u32 length]*[value... elements]
 * Byte 210: Thread  - [u8 state][u32 frames]*[[value callee][value env]
 *  [u32 pcoffset][u16 ret][u16 args][u16 size]*[value ...stack]
 * Byte 211: Object  - [value parent][u32 length]*2*[value... kvs]
 * Byte 212: FuncDef - [u32 locals][u32 arity][u32 flags][u32 literallen]*[value...
 *  literals][u32 bytecodelen]*[u16... bytecode]
 * Byte 213: FunEnv  - [value thread][u32 length]*[value ...upvalues]
 *  (upvalues is not read if thread is a thread object)
 * Byte 214: Func    - [value parent][value def][value env]
 *  (nil values indicate empty)
 * Byte 215: LUdata  - [value meta][u32 length]*[u8... bytes]
 * Byte 216: CFunc   - [u32 length]*[u8... idstring]
 * Byte 217: Ref     - [u32 id]
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
static uint32_t bytes2dbl(const uint8_t *bytes) {
    union {
        uint8_t bytes[8];
        double dbl;
    } u;
    gst_memcpy(u.bytes, bytes, 8 * sizeof(uint8_t));
    return u.dbl;
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
        GstValue *out) {

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

    /* Check enough buffer left to read one byte */
    if (data >= end) deser_error(UEB);

    /* Small integer */
    if (*data < 201) {
        ret.type = GST_NUMBER;
        ret.data.number = *data - 100; 
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
            ret.type = GST_NUMBER; 
            read_dbl(ret.data.number);
            break;

        case 205: /* String */
            ret.type = GST_STRING;
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
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, &k)))
                    return err; 
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, &v)))
                    return err; 
                gst_struct_put(buffer, k, v);
            }
            ret.data.st = gst_struct_end(vm, buffer);
            gst_array_push(vm, visited, ret);
            break;

        case 207: /* Buffer */
            ret.type = GST_BYTEBUFFER;
            read_u32(length);
            deser_datacheck(length);
            ret.data.buffer = gst_alloc(vm, sizeof(GstBuffer));
            ret.data.buffer->data = gst_alloc(vm, length);
            gst_memcpy(ret.data.buffer->data, data, length);
            ret.data.buffer->count = length;
            ret.data.buffer->capacity = length;
            data += length;
            gst_array_push(vm, visited, ret);
            break;

        case 208: /* Array */
            ret.type = GST_ARRAY;
            read_u32(length);
            buffer = gst_alloc(vm, length * sizeof(GstValue));
            for (i = 0; i < length; ++i)
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, buffer + i)))
                    return err; 
            ret.data.array = gst_alloc(vm, sizeof(GstArray));
            ret.data.array->data = buffer;
            ret.data.array->count = length;
            ret.data.array->capacity = length;
            gst_array_push(vm, visited, ret);
            break;

        case 209: /* Tuple */
            ret.type = GST_TUPLE;
            read_u32(length);
            buffer = gst_tuple_begin(vm, length);
            for (i = 0; i < length; ++i)
                if ((err = gst_deserialize_impl(vm, data, end, &data, visited, buffer + i)))
                    return err;
            ret.type = GST_TUPLE;
            ret.data.tuple = gst_tuple_end(vm, buffer);
            gst_array_push(vm, visited, ret);
            break;

        case 210: /* Thread */
            {
                GstValue nil;
                GstThread *t;
                GstValue *stack;
                uint16_t prevsize = 0;
                uint8_t statusbyte;
                nil.type = GST_NIL;
                t = gst_thread(vm, nil, 64);
                ret.type = GST_THREAD;
                ret.data.thread = t;
                deser_assert(data < end, UEB);
                statusbyte = *data++;
                read_u32(length);
                /* Check for empty thread - TODO check for valid state */
                if (length == 0)
                    break;
                /* Set status */
                if (statusbyte == 0) t->status = GST_THREAD_PENDING;
                else if (statusbyte == 1) t->status = GST_THREAD_ALIVE;
                else t->status = GST_THREAD_DEAD;
                /* Add frames */
                for (i = 0; i < length; ++i) {
                    GstValue callee, env;
                    uint32_t pcoffset;
                    uint16_t ret, args, size, j;
                    /* Create a new frame */
                    if (i > 0)
                        gst_thread_beginframe(vm, t, nil, 0);
                    /* Read the stack */
                    err = gst_deserialize_impl(vm, data, end, &data, visited, &callee);
                    if (err != NULL) return err;
                    err = gst_deserialize_impl(vm, data, end, &data, visited, &env);
                    if (err != NULL) return err;
                    read_u32(pcoffset);
                    read_u16(ret);
                    read_u16(args);
                    read_u16(size);
                    /* Set up the stack */
                    stack = gst_thread_stack(t);
                    if (callee.type == GST_FUNCTION) {
                        gst_frame_pc(stack) = callee.data.function->def->byteCode + pcoffset;
                        if (env.type == GST_FUNCENV)
                            gst_frame_env(stack) = env.data.env;
                    }
                    gst_frame_ret(stack) = ret;
                    gst_frame_args(stack) = args;
                    gst_frame_size(stack) = size;
                    gst_frame_prevsize(stack) = prevsize;
                    prevsize = size;
                    /* Push stack args */
                    for (j = 0; j < size; ++j) {
                        GstValue temp;
                        err = gst_deserialize_impl(vm, data, end, &data, visited, &temp);
                        gst_thread_push(vm, t, temp);
                    }
                }
            }
            break;

        case 211: /* Object */
            {
                GstValue parent;
                ret.type = GST_OBJECT;
                ret.data.object = gst_object(vm, 10);
                err = gst_deserialize_impl(vm, data, end, &data, visited, &parent);
                if (err != NULL) return err;
                read_u32(length);
                for (i = 0; i < length; i += 2) {
                    GstValue key, value;
                    err = gst_deserialize_impl(vm, data, end, &data, visited, &key);
                    if (err != NULL) return err;
                    err = gst_deserialize_impl(vm, data, end, &data, visited, &value);
                    if (err != NULL) return err;
                    gst_object_put(vm, ret.data.object, key, value);
                }
                if (parent.type == GST_OBJECT)
                    ret.data.object->parent = parent.data.object; 
                gst_array_push(vm, visited, ret);
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
                ret.type = GST_FUNCDEF;
                ret.data.def = def;
                def->locals = locals;
                def->arity = arity;
                def->flags = flags;
                def->literalsLen = literalsLen;
                if (literalsLen > 0) {
                    def->literals = gst_alloc(vm, literalsLen * sizeof(GstValue));
                }
                for (i = 0; i < literalsLen; ++i) {
                    err = gst_deserialize_impl(vm, data, end, &data, visited, def->literals + i);
                    if (err != NULL) return err;
                }
                read_u32(byteCodeLen);
                deser_datacheck(byteCodeLen);
                def->byteCode = gst_alloc(vm, byteCodeLen * sizeof(uint16_t));
                def->byteCodeLen = byteCodeLen;
                for (i = 0; i < byteCodeLen; ++i) {
                    read_u16(def->byteCode[i]);
                }
                gst_array_push(vm, visited, ret);
            }
            break;

        case 213: /* Funcenv */
            {
                GstValue thread;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &thread);
                if (err != NULL) return err;
                read_u32(length);
                ret.type = GST_FUNCENV;
                ret.data.env = gst_alloc(vm, sizeof(GstFuncEnv));
                ret.data.env->stackOffset = length;
                if (thread.type == GST_THREAD) {
                    ret.data.env->thread = thread.data.thread;
                } else {
                    ret.data.env->thread = NULL;
                    ret.data.env->values = gst_alloc(vm, sizeof(GstValue) * length);
                    for (i = 0; i < length; ++i) {
                        GstValue item;
                        err = gst_deserialize_impl(vm, data, end, &data, visited, &item);
                        if (err != NULL) return err;
                        ret.data.env->values[i] = item;
                    }
                }
                gst_array_push(vm, visited, ret);
            }
            break;

        case 214: /* Function */
            {
                GstValue parent, def, env;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &parent);
                if (err != NULL) return err;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &def);
                if (err != NULL) return err;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &env);
                if (err != NULL) return err;
                ret.type = GST_FUNCTION;
                ret.data.function = gst_alloc(vm, sizeof(GstFunction));
                if (parent.type == GST_NIL) {
                    ret.data.function->parent = NULL;
                } else if (parent.type == GST_FUNCTION) {
                    ret.data.function->parent = parent.data.function;
                } else {
                    deser_error("expected function");
                }
                deser_assert(def.type == GST_FUNCDEF, "expected funcdef");
                deser_assert(env.type == GST_FUNCENV, "expected funcenv");
                ret.data.function->env = env.data.env;
                ret.data.function->def = env.data.def;
                gst_array_push(vm, visited, ret);
            }
            break;

        case 215: /* LUdata */
            {
                GstValue meta;
                ret.type = GST_USERDATA;
                err = gst_deserialize_impl(vm, data, end, &data, visited, &meta);
                if (err != NULL) return err;
                deser_assert(meta.type == GST_STRUCT, "userdata requires valid meta struct");
                read_u32(length);
                deser_datacheck(length);
                ret.data.pointer = gst_userdata(vm, length, meta.data.st);
                gst_memcpy(ret.data.pointer, data, length);
                gst_array_push(vm, visited, ret);
            }
            break;

        case 216: /* C function */
            /* TODO - add registry for c functions */
            read_u32(length);
            ret.type = GST_NIL;
            break;

        case 217: /* Reference */
            read_u32(length);
            deser_assert(visited->count > length, "invalid reference");
            ret = visited->data[length];
            break;
    }

    /* Handle a successful return */
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
        const uint8_t *nextData) {
    GstValue ret;
    const char *err;
    GstArray *visited = gst_array(vm, 10);
    err = gst_deserialize_impl(vm, data, data + len, &nextData, visited, &ret);
    if (err != NULL) return err;
    *out = ret;
    return NULL;
}
    
/* Allow appending other types to buffers */
BUFFER_DEFINE(number, GstNumber)
/*BUFFER_DEFINE(u16, uint16_t)*/
BUFFER_DEFINE(u32, uint32_t)

/* Serialize a value and write to a buffer. Returns possible
 * error messages. */
const char *gst_serialize_impl(
        Gst *vm,
        GstBuffer *buffer,
        GstObject *visited,
        uint32_t *nextId,
        GstValue x) {

    uint32_t i, count;
    const char *err;
    GstValue check;

#define write_byte(b) gst_buffer_push(vm, buffer, (b))
#define write_u32(b) gst_buffer_push_u32(vm, buffer, (b))
#define write_u16(b) gst_buffer_push_u16(vm, buffer, (b))
#define write_dbl(b) gst_buffer_push_number(vm, buffer, (b))

    /* Check non reference types - if successful, return NULL */
    switch (x.type) {
        case GST_NIL:
            write_byte(201);
            return NULL;
        case GST_BOOLEAN:
            write_byte(x.data.boolean ? 202 : 203);
            return NULL;
        case GST_NUMBER: 
            {
                GstNumber number = x.data.number;
                int32_t int32Num = (int32_t) number;
                if (number == (GstNumber) int32Num &&
                        int32Num <= 100 && int32Num >= -100) {
                    write_byte(int32Num + 100);
                } else {
                    write_byte(204);
                    write_dbl(number);
                }
            }
            return NULL;
        case GST_CFUNCTION:
            /* TODO */
            break;
        default:
            break;
    }

    /* Check if already seen - if so, use reference */
    check = gst_object_get(visited, x);
    if (check.type == GST_NUMBER) {
        write_byte(217);
        write_u32((uint32_t) check.data.number);
        return NULL;
    }

    /* Check reference types */
    switch (x.type) {
        default:
           return "unable to serialize type"; 
        case GST_STRING:
           write_byte(205);
           count = gst_string_length(x.data.string);
           write_u32(count);
           for (i = 0; i < count; ++i) {
               write_byte(x.data.string[i]);
           }
           break;
        case GST_STRUCT:
           write_byte(206);
           count = gst_struct_length(x.data.st);
           write_u32(count);
           for (i = 0; i < gst_struct_capacity(x.data.st); i += 2) {
               if (x.data.st[i].type != GST_NIL) {
                   err = gst_serialize_impl(vm, buffer, visited, nextId, x.data.st[i]); 
                   if (err != NULL) return err;
                   err = gst_serialize_impl(vm, buffer, visited, nextId, x.data.st[i + 1]); 
                   if (err != NULL) return err;
               }
           }
           break;
        case GST_BYTEBUFFER:
           write_byte(207);
           count = x.data.buffer->count;
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
               err = gst_serialize_impl(vm, buffer, visited, nextId, x.data.array->data[i]); 
               if (err != NULL) return err;
           }
           break;
        case GST_TUPLE:
           write_byte(209);
           count = gst_tuple_length(x.data.tuple);
           write_u32(count);
           for (i = 0; i < count; ++i) {
               err = gst_serialize_impl(vm, buffer, visited, nextId, x.data.tuple[i]); 
               if (err != NULL) return err;
           }
           break;
        /*case GST_THREAD:*/
           /*break;*/
    }

    /* Record reference */
    check.type = GST_NUMBER;
    check.data.number = *nextId++;
    gst_object_put(vm, visited, x, check);

    /* Return success */
    return NULL;
}

/* Serialize an object */
const char *gst_serialize(Gst *vm, GstBuffer *buffer, GstValue x) {
    uint32_t nextId = 0;
    uint32_t oldCount = buffer->count;
    const char *err;
    GstObject *visited = gst_object(vm, 10);
    err = gst_serialize_impl(vm, buffer, visited, &nextId, x);
    if (err != NULL) {
        buffer->count = oldCount;
    }
    return err;
}
