#include <gst/datatypes.h>
#include <gst/vm.h>
#include <gst/values.h>
#include <gst/ds.h>
#include <gst/util.h>

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
 * Byte 206: Buffer  - [u32 length]*[u8... characters]
 * Byte 207: Array   - [u32 length]*[value... elements]
 * Byte 208: Tuple   - [u32 length]*[value... elements]
 * Byte 209: Thread  - [u8 state][u32 frames]*[[value callee][value env]
 *  [u32 pcoffset][u32 erroffset][u16 ret][u16 errloc][u16 size]*[value ...stack]
 * Byte 210: Object  - [value meta][u32 length]*2*[value... kvs]
 * Byte 211: FuncDef - [u32 locals][u32 arity][u32 flags][u32 literallen]*[value...
 *  literals][u32 bytecodelen]*[u16... bytecode]
 * Byte 212: FunEnv  - [value thread][u32 length]*[value ...upvalues]
 *  (upvalues is not read if thread is a thread object)
 * Byte 213: Func    - [value parent][value def][value env]
 *  (nil values indicate empty)
 * Byte 214: LUdata  - [value meta][u32 length]*[u8... bytes]
 * Byte 215: CFunc   - [u32 length]*[u8... idstring]
 * Byte 216: Ref     - [u32 id]
 */

/* Error at buffer end */
static const char UEB[] = "unexpected end of buffer";

/* Read 4 bytes as an unsigned integer */
static uint32_t bytes2u32(uint8_t bytes[4]) {
    union {
        uint8_t bytes[4];
        uint32_t u32;
    } u;
    u.bytes = bytes;
    return u.u32;
}

/* Read 2 bytes as unsigned short */
static uint16_t bytes2u16(uint8_t bytes[2]) {
    union {
        uint8_t bytes[2];
        uint16_t u16;
    } u;
    u.bytes = bytes;
    return u.u16;
}

/* Read 8 bytes as a double */
static uint32_t bytes2dbl(uint8_t bytes[8]) {
    union {
        uint8_t bytes[8];
        double dbl;
    } u;
    u.bytes = bytes;
    return u.dbl;
}

/* Read a string and turn it into a gst value */
static GstValue gst_deserialize_impl(
        Gst *vm, 
        uint8_t *data,
        uint8_t *end,
        uint8_t **newData,
        GstArray *visited) {

    GstValue ret;
    ret.type = GST_NIL;
    GstValue *buffer;
    uint32_t length, i;

    /* Handle errors as needed */
#define deser_error(e) (exit(-1), NULL)

    /* Assertions */
#define deser_assert(c, e) do{if(!(c))deser_error(e);}while(0)

    /* Assert enough buffer */
#define deser_datacheck(len) (end - data < (len) ? deser_error(UEB) : data)

    /* Check for enough space to read uint32_t */
#define read_u32() (deser_datacheck(4), bytes2u32(data))

    /* Check for enough space to read uint16_t */
#define read_u16() (deser_datacheck(2), bytes2u16(data))

    /* Check for enough space to read uint32_t */
#define read_dbl() (deser_datacheck(8), bytes2dbl(data))

    /* Check enough buffer left to read one byte */
    if (data >= end) deser_error(UEB);

    /* Small integer */
    if (*data < 201) {
        ret.type = GST_NUMBER;
        ret.data.number = *data - 100; 
        newData = data + 1;
        return ret;
    } 

    /* Main switch for types */
    switch (*data++) {

        case 201: /* Nil */
            ret.type = GST_NIL;
            *newData = data;
            return ret;

        case 202: /* True */
            ret.type = GST_BOOLEAN;
            ret.data.boolean = 1;
            *newData = data;
            return ret;

        case 203: /* False */
            ret.type = GST_BOOLEAN;
            ret.data.boolean = 0;
            *newData = data;
            return ret;

        case 204: /* Long number (double) */
            ret.type = GST_NUMBER; 
            ret.data.number = read_dbl();
            *newData = data + 8;
            return ret;

        case 205: /* String */
            ret.type = GST_STRING;
            length = read_u32(); data += 4;
            data = deser_datacheck(length);
            ret.data.string = 
                gst_alloc(vm, 2 * sizeof(uint32_t) + length + 1) + 2 * sizeof(uint32_t);
            gst_string_length(ret.data.string) = length;
            gst_string_hash(ret.data.string) = 0;
            gst_memcpy(ret.data.string, data, length);
            ret.data.string[length] = 0;
            *newData = data + length;
            gst_array_push(vm, visited, ret);
            return ret;

        case 206: /* Buffer */
            ret.type = GST_BUFFER;
            length = read_u32(); data += 4;
            data = deser_datacheck(length);
            ret.data.buffer = gst_alloc(vm, sizeof(GstBuffer));
            ret.data.buffer->data = gst_alloc(vm, length);
            gst_memcpy(ret.data.string, data, length;
            ret.data.buffer->count = length;
            ret.data.buffer->capacity = length;
            *newData = data + length;
            gst_array_push(vm, visited, ret);
            return ret;

        case 207: /* Array */
            ret.type = GST_ARRAY;
            length = read_u32(); data += 4;
            buffer = gst_alloc(vm, length * sizeof(GstValue));
            for (i = 0; i < length; ++i)
                buffer[i] = gst_deserialize_impl(vm, data, end, &data, visited);
            ret.data.array = gst_alloc(vm, sizeof(GstArray));
            ret.data.array->data = buffer;
            ret.data.array->count = length;
            ret.data.array->capacity = length;
            *newData = data;
            gst_array_push(vm, visited, ret);
            return ret;

        case 208: /* Tuple */
            ret.type = GST_TUPLE;
            length = read_u32(); data += 4;
            buffer = gst_alloc(vm, length * sizeof(GstValue) + 2 * sizeof(uint32_t))
                + 2 * sizeof(uint32_t);
            for (i = 0; i < length; ++i)
                buffer[i] = gst_deserialize_impl(vm, data, end, &data, visited);
            gst_tuple_hash(buffer) = 0;
            gst_tuple_length(buffer) = length;
            ret.data.tuple = buffer;
            *newData = data;
            gst_array_push(vm, visited, ret);
            return ret;

        case 209: /* Thread */
            {
                GstValue nil;
                GstThread *t;
                GstValue *stack;
                uint16_t prevsize = 0;
                uint8 statusbyte;
                nil.type = GST_NIL;
                t = gst_thread(vm, 64, nil);
                ret.type = GST_THREAD;
                ret.data.thread = t;
                deser_assert(data < end, UEB);
                statusbyte = *data++;
                length = read_u32(); data += 4;
                /* Check for empty thread */
                if (length == 0) {
                    *newData = data;
                    return ret;
                } 
                /* Set status */
                if (statusbyte == 0) t->status = GST_THREAD_PENDING;
                else if (statusbyte == 1) t->status = GST_THREAD_ALIVE;
                else t->status = GST_THREAD_DEAD;
                /* Add frames */
                for (i = 0; i < length; ++i) {
                    GstValue callee, env;
                    uint32_t pcoffset, erroffset;
                    uint16_t ret, errloc, size, j;
                    /* Create a new frame */
                    if (i > 0)
                        gst_thread_beginframe(vm, t, nil, 0);
                    /* Read the stack */
                    callee = gst_deserialize_impl(vm, data, end, &data, visited);
                    env = gst_deserialize_impl(vm, data, end, &data, visited);
                    pcoffset = read_u32(); data += 4;
                    erroffset = read_u32(); data += 4;
                    ret = read_u16(); data += 2;
                    errloc = read_u16(); data += 2;
                    size = read_u16(); data += 2;
                    /* Set up the stack */
                    stack = gst_thread_stack(t);
                    if (callee.type = GST_FUNCTION) {
                        gst_frame_pc(stack) = callee.data.function->def->byteCode + pcoffset;
                        gst_frame_errjmp(stack) = callee.data.function->def->byteCode + erroffset;
                        if (env.type == GST_FUNCENV)
                            gst_frame_env(stack) = env.data.env;
                    }
                    gst_frame_ret(stack) = ret;
                    gst_frame_errloc(stack) = errloc;
                    gst_frame_size(stack) = size;
                    gst_frame_prevsize(stack) = prevsize;
                    prevsize = size;
                    /* Push stack args */
                    for (j = 0; j < size; ++j) {
                        GstValue temp = gst_deserialize_impl(vm, data, end, &data, visited);
                        gst_thread_push(vm, t, temp);
                    }
                }
                *newData = data;
            }
            return ret;

        case 210: /* Object */
            {
                GstValue meta;
                ret.type = GST_OBJECT;
                ret.data.object = gst_object(vm, 10);
                meta = gst_deserialize_impl(vm, data, end, &data, visited);
                length = read_u32(); data += 4;
                for (i = 0; i < length; i += 2) {
                    GstValue key, value;
                    key = gst_deserialize_impl(vm, data, end, &data, visited);
                    value = gst_deserialize_impl(vm, data, end, &data, visited);
                    gst_object_put(vm, ret.data.object, key, value);
                }
                if (meta.type == GST_OBJECT) {
                    ret.data.object->meta = meta.data.object; 
                }
                *newData = data;
                gst_array_push(vm, visited, ret);
            }
            return ret;

        case 211: /* Funcdef */
            {
                GstFuncDef *def;
                uint32_t locals, arity, literalsLen, byteCodeLen, flags;
                locals = read_u32(); data += 4;
                arity = read_u32(); data += 4;
                flags = read_u32(); data += 4;
                literalsLen = read_u32(); data += 4;
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
                    def->literals[i] = gst_deserialize_impl(vm, data, end, &data, visited);
                }
                byteCodeLen = read_u32(); data += 4;
                deser_datacheck(byteCodeLen);
                def->byteCode = vm_alloc(vm, byteCodeLen * sizeof(uint16_t));
                def->byteCodeLen = byteCodeLen;
                for (i = 0; i < byteCodeLen; ++i) {
                    def->byteCode[i] = read_u16();
                    data += 2;    
                }
                *newData = data;
                gst_array_push(vm, visited, ret);
            }
            return ret;

        case 212: /* Funcenv */
            {
                GstValue thread = gst_deserialize_impl(vm, deata, &data, visited);
                length = read_u32(); data += 4;
                ret.type = GST_FUNCENV;
                ret.data.env = gst_alloc(vm, sizeof(GstFuncEnv));
                ret.data.env->stackOffset = length;
                if (thread.type == GST_THREAD) {
                    ret.data.env->thread = thread.data.thread;
                } else {
                    ret.data.env->thread = NULL;
                    ret.data.env->values = vm_alloc(vm, sizeof(GstValue) * length);
                    for (i = 0; i < length; ++i) {
                        GstValue item = gst_deserialize_impl(vm, data, end, &data, visited);
                        ret.data.env->values[i] = item;
                    }
                }
                *newData = data;
                gst_array_push(vm, visited, ret);
            }
            return ret;

        case 213: /* Function */
            {
                GstValue parent, def, env;
                parent = gst_deserialize_impl(vm, data, end, &data, visited);
                def = gst_deserialize_impl(vm, data, end, &data, visited);
                env = gst_deserialize_impl(vm, data, end, &data, visited);
                ret.type = GST_FUNCTION;
                ret.data.function = gst_alloc(vm, sizeof(GstFunction));
                if (parent->type == GST_NIL) {
                    ret.data.function->parent = NULL;
                } else if (parent->type == GST_FUNCTION) {
                    ret.data.function->parent = parent.data.function;
                } else {
                    deser_error("expected function");
                }
                gst_assert(def->type == GST_FUNCDEF, "expected funcdef");
                gst_assert(env->type == GST_FUNCENV, "expected funcenv");
                ret.data.function->env = env.data.env;
                ret.data.function->def = env.data.def;
                *newData = data;
                gst_array_push(vm, visited, ret);
            }
            return ret;

        case 214: /* LUdata */
            {
                GstValue meta;
                ret.type = GST_USERDATA;
                meta = gst_deserialize_impl(vm, data, end, &data, visited);
                deser_assert(meta.type == GST_OBJECT, "userdata requires valid meta");
                length = read_u32(); data += 4;
                data = deser_datacheck(length);
                ret.data.pointer = gst_userdata(vm, length, meta.data.object);
                gst_memcpy(ret.data.pointer, data, length);
                *newData = data + length;
                gst_array_push(vm, visited, ret);
            }
            return ret;

        case 215: /* C function */

            return ret;

        case 216: /* Reference */
            length = read_u32(); data += 4;
            deser_assert(visited->count > length, "invalid reference");
            *newData = data;
            return visited->data[length];
    }
}
