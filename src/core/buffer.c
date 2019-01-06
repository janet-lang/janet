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

#include <janet/janet.h>
#include "gc.h"
#include "util.h"

/* Initialize a buffer */
JanetBuffer *janet_buffer_init(JanetBuffer *buffer, int32_t capacity) {
    uint8_t *data = NULL;
    if (capacity > 0) {
        data = malloc(sizeof(uint8_t) * capacity);
        if (NULL == data) {
            JANET_OUT_OF_MEMORY;
        }
    }
    buffer->count = 0;
    buffer->capacity = capacity;
    buffer->data = data;
    return buffer;
}

/* Deinitialize a buffer (free data memory) */
void janet_buffer_deinit(JanetBuffer *buffer) {
    free(buffer->data);
}

/* Initialize a buffer */
JanetBuffer *janet_buffer(int32_t capacity) {
    JanetBuffer *buffer = janet_gcalloc(JANET_MEMORY_BUFFER, sizeof(JanetBuffer));
    return janet_buffer_init(buffer, capacity);
}

/* Ensure that the buffer has enough internal capacity */
void janet_buffer_ensure(JanetBuffer *buffer, int32_t capacity, int32_t growth) {
    uint8_t *new_data;
    uint8_t *old = buffer->data;
    if (capacity <= buffer->capacity) return;
    capacity *= growth;
    new_data = realloc(old, capacity * sizeof(uint8_t));
    if (NULL == new_data) {
        JANET_OUT_OF_MEMORY;
    }
    buffer->data = new_data;
    buffer->capacity = capacity;
}

/* Ensure that the buffer has enough internal capacity */
void janet_buffer_setcount(JanetBuffer *buffer, int32_t count) {
    if (count < 0)
        return;
    if (count > buffer->count) {
        int32_t oldcount = buffer->count;
        janet_buffer_ensure(buffer, count, 1);
        memset(buffer->data + oldcount, 0, count - oldcount);
    }
    buffer->count = count;
}

/* Adds capacity for enough extra bytes to the buffer. Ensures that the
 * next n bytes pushed to the buffer will not cause a reallocation */
void janet_buffer_extra(JanetBuffer *buffer, int32_t n) {
    /* Check for buffer overflow */
    if ((int64_t)n + buffer->count > INT32_MAX) {
        janet_panic("buffer overflow");
    }
    int32_t new_size = buffer->count + n;
    if (new_size > buffer->capacity) {
        int32_t new_capacity = new_size * 2;
        uint8_t *new_data = realloc(buffer->data, new_capacity * sizeof(uint8_t));
        if (NULL == new_data) {
            JANET_OUT_OF_MEMORY;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
}

/* Push a cstring to buffer */
void janet_buffer_push_cstring(JanetBuffer *buffer, const char *cstring) {
    int32_t len = 0;
    while (cstring[len]) ++len;
    janet_buffer_push_bytes(buffer, (const uint8_t *) cstring, len);
}

/* Push multiple bytes into the buffer */
void janet_buffer_push_bytes(JanetBuffer *buffer, const uint8_t *string, int32_t length) {
    janet_buffer_extra(buffer, length);
    memcpy(buffer->data + buffer->count, string, length);
    buffer->count += length;
}

void janet_buffer_push_string(JanetBuffer *buffer, const uint8_t *string) {
    janet_buffer_push_bytes(buffer, string, janet_string_length(string));
}

/* Push a single byte to the buffer */
void janet_buffer_push_u8(JanetBuffer *buffer, uint8_t byte) {
    janet_buffer_extra(buffer, 1);
    buffer->data[buffer->count] = byte;
    buffer->count++;
}

/* Push a 16 bit unsigned integer to the buffer */
void janet_buffer_push_u16(JanetBuffer *buffer, uint16_t x) {
    janet_buffer_extra(buffer, 2);
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->count += 2;
}

/* Push a 32 bit unsigned integer to the buffer */
void janet_buffer_push_u32(JanetBuffer *buffer, uint32_t x) {
    janet_buffer_extra(buffer, 4);
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->data[buffer->count + 2] = (x >> 16) & 0xFF;
    buffer->data[buffer->count + 3] = (x >> 24) & 0xFF;
    buffer->count += 4;
}

/* Push a 64 bit unsigned integer to the buffer */
void janet_buffer_push_u64(JanetBuffer *buffer, uint64_t x) {
    janet_buffer_extra(buffer, 8);
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->data[buffer->count + 2] = (x >> 16) & 0xFF;
    buffer->data[buffer->count + 3] = (x >> 24) & 0xFF;
    buffer->data[buffer->count + 4] = (x >> 32) & 0xFF;
    buffer->data[buffer->count + 5] = (x >> 40) & 0xFF;
    buffer->data[buffer->count + 6] = (x >> 48) & 0xFF;
    buffer->data[buffer->count + 7] = (x >> 56) & 0xFF;
    buffer->count += 8;
}

/* C functions */

static Janet cfun_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getinteger(argv, 0);
    JanetBuffer *buffer = janet_buffer(cap);
    return janet_wrap_buffer(buffer);
}

static Janet cfun_u8(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    for (i = 1; i < argc; i++) {
        janet_buffer_push_u8(buffer, (uint8_t) (janet_getinteger(argv, i) & 0xFF));
    }
    return argv[0];
}

static Janet cfun_word(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    for (i = 1; i < argc; i++) {
        double number = janet_getnumber(argv, 0);
        uint32_t word = (uint32_t) number;
        if (word != number)
            janet_panicf("cannot convert %v to machine word", argv[0]);
        janet_buffer_push_u32(buffer, word);
    }
    return argv[0];
}

static Janet cfun_chars(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    for (i = 1; i < argc; i++) {
        JanetByteView view = janet_getbytes(argv, i);
        janet_buffer_push_bytes(buffer, view.bytes, view.len);
    }
    return argv[0];
}

static Janet cfun_clear(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    buffer->count = 0;
    return argv[0];
}

static Janet cfun_popn(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    int32_t n = janet_getinteger(argv, 1);
    if (n < 0) janet_panic("n must be non-negative");
    if (buffer->count < n) {
        buffer->count = 0;
    } else {
        buffer->count -= n;
    }
    return argv[0];
}

static Janet cfun_slice(int32_t argc, Janet *argv) {
    JanetRange range = janet_getslice(argc, argv);
    JanetByteView view = janet_getbytes(argv, 0);
    JanetBuffer *buffer = janet_buffer(range.end - range.start);
    memcpy(buffer->data, view.bytes + range.start, range.end - range.start);
    buffer->count = range.end - range.start;
    return janet_wrap_buffer(buffer);
}

static const JanetReg cfuns[] = {
    {"buffer/new", cfun_new,
        JDOC("(buffer/new capacity)\n\n"
                "Creates a new, empty buffer with enough memory for capacity bytes. "
                "Returns a new buffer.")
    },
    {"buffer/push-byte", cfun_u8,
        JDOC("(buffer/push-byte buffer x)\n\n"
                "Append a byte to a buffer. Will expand the buffer as necessary. "
                "Returns the modified buffer. Will throw an error if the buffer overflows.")
    },
    {"buffer/push-word", cfun_word,
        JDOC("(buffer/push-word buffer x)\n\n"
                "Append a machine word to a buffer. The 4 bytes of the integer are appended "
                "in twos complement, big endian order, unsigned. Returns the modified buffer. Will "
                "throw an error if the buffer overflows.")
    },
    {"buffer/push-string", cfun_chars,
        JDOC("(buffer/push-string buffer str)\n\n"
                "Push a string onto the end of a buffer. Non string values will be converted "
                "to strings before being pushed. Returns the modified buffer. "
                "Will throw an error if the buffer overflows.")
    },
    {"buffer/popn", cfun_popn,
        JDOC("(buffer/popn buffer n)\n\n"
                "Removes the last n bytes from the buffer. Returns the modified buffer.")
    },
    {"buffer/clear", cfun_clear,
        JDOC("(buffer/clear buffer)\n\n"
                "Sets the size of a buffer to 0 and empties it. The buffer retains "
                "its memory so it can be efficiently refilled. Returns the modified buffer.")
    },
    {"buffer/slice", cfun_slice,
        JDOC("(buffer/slice bytes [, start=0 [, end=(length bytes)]])\n\n"
                "Takes a slice of a byte sequence from start to end. The range is half open, "
                "[start, end). Indexes can also be negative, indicating indexing from the end of the "
                "end of the array. By default, start is 0 and end is the length of the buffer. "
                "Returns a new buffer.")
    },
    {NULL, NULL, NULL}
};

void janet_lib_buffer(JanetTable *env) {
    janet_cfuns(env, NULL, cfuns);
}
