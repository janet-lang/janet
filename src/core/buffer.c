/*
* Copyright (c) 2020 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "gc.h"
#include "util.h"
#include "state.h"
#endif

/* Initialize a buffer */
JanetBuffer *janet_buffer_init(JanetBuffer *buffer, int32_t capacity) {
    uint8_t *data = NULL;
    if (capacity > 0) {
        janet_gcpressure(capacity);
        data = malloc(sizeof(uint8_t) * (size_t) capacity);
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
    int64_t big_capacity = ((int64_t) capacity) * growth;
    capacity = big_capacity > INT32_MAX ? INT32_MAX : (int32_t) big_capacity;
    janet_gcpressure(capacity - buffer->capacity);
    new_data = realloc(old, (size_t) capacity * sizeof(uint8_t));
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
        janet_gcpressure(new_capacity - buffer->capacity);
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
    if (0 == length) return;
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

static Janet cfun_buffer_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getinteger(argv, 0);
    JanetBuffer *buffer = janet_buffer(cap);
    return janet_wrap_buffer(buffer);
}

static Janet cfun_buffer_new_filled(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    int32_t count = janet_getinteger(argv, 0);
    int32_t byte = 0;
    if (argc == 2) {
        byte = janet_getinteger(argv, 1) & 0xFF;
    }
    JanetBuffer *buffer = janet_buffer(count);
    if (buffer->data)
        memset(buffer->data, byte, count);
    buffer->count = count;
    return janet_wrap_buffer(buffer);
}

static Janet cfun_buffer_fill(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    int32_t byte = 0;
    if (argc == 2) {
        byte = janet_getinteger(argv, 1) & 0xFF;
    }
    if (buffer->count) {
        memset(buffer->data, byte, buffer->count);
    }
    return argv[0];
}

static Janet cfun_buffer_trim(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    if (buffer->count) {
        if (buffer->count < buffer->capacity) {
            uint8_t *newData = realloc(buffer->data, buffer->count);
            if (NULL == newData) {
                JANET_OUT_OF_MEMORY;
            }
            buffer->data = newData;
            buffer->capacity = buffer->count;
        }
    } else {
        buffer->capacity = 0;
        free(buffer->data);
        buffer->data = NULL;
    }
    return argv[0];
}

static Janet cfun_buffer_u8(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    for (i = 1; i < argc; i++) {
        janet_buffer_push_u8(buffer, (uint8_t)(janet_getinteger(argv, i) & 0xFF));
    }
    return argv[0];
}

static Janet cfun_buffer_word(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    for (i = 1; i < argc; i++) {
        double number = janet_getnumber(argv, i);
        uint32_t word = (uint32_t) number;
        if (word != number)
            janet_panicf("cannot convert %v to machine word", argv[i]);
        janet_buffer_push_u32(buffer, word);
    }
    return argv[0];
}

static Janet cfun_buffer_chars(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    for (i = 1; i < argc; i++) {
        JanetByteView view = janet_getbytes(argv, i);
        if (view.bytes == buffer->data) {
            janet_buffer_ensure(buffer, buffer->count + view.len, 2);
            view.bytes = buffer->data;
        }
        janet_buffer_push_bytes(buffer, view.bytes, view.len);
    }
    return argv[0];
}

static Janet cfun_buffer_clear(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    buffer->count = 0;
    return argv[0];
}

static Janet cfun_buffer_popn(int32_t argc, Janet *argv) {
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

static Janet cfun_buffer_slice(int32_t argc, Janet *argv) {
    JanetByteView view = janet_getbytes(argv, 0);
    JanetRange range = janet_getslice(argc, argv);
    JanetBuffer *buffer = janet_buffer(range.end - range.start);
    if (buffer->data)
        memcpy(buffer->data, view.bytes + range.start, range.end - range.start);
    buffer->count = range.end - range.start;
    return janet_wrap_buffer(buffer);
}

static void bitloc(int32_t argc, Janet *argv, JanetBuffer **b, int32_t *index, int *bit) {
    janet_fixarity(argc, 2);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    double x = janet_getnumber(argv, 1);
    int64_t bitindex = (int64_t) x;
    int64_t byteindex = bitindex >> 3;
    int which_bit = bitindex & 7;
    if (bitindex != x || bitindex < 0 || byteindex >= buffer->count)
        janet_panicf("invalid bit index %v", argv[1]);
    *b = buffer;
    *index = (int32_t) byteindex;
    *bit = which_bit;
}

static Janet cfun_buffer_bitset(int32_t argc, Janet *argv) {
    int bit;
    int32_t index;
    JanetBuffer *buffer;
    bitloc(argc, argv, &buffer, &index, &bit);
    buffer->data[index] |= 1 << bit;
    return argv[0];
}

static Janet cfun_buffer_bitclear(int32_t argc, Janet *argv) {
    int bit;
    int32_t index;
    JanetBuffer *buffer;
    bitloc(argc, argv, &buffer, &index, &bit);
    buffer->data[index] &= ~(1 << bit);
    return argv[0];
}

static Janet cfun_buffer_bitget(int32_t argc, Janet *argv) {
    int bit;
    int32_t index;
    JanetBuffer *buffer;
    bitloc(argc, argv, &buffer, &index, &bit);
    return janet_wrap_boolean(buffer->data[index] & (1 << bit));
}

static Janet cfun_buffer_bittoggle(int32_t argc, Janet *argv) {
    int bit;
    int32_t index;
    JanetBuffer *buffer;
    bitloc(argc, argv, &buffer, &index, &bit);
    buffer->data[index] ^= (1 << bit);
    return argv[0];
}

static Janet cfun_buffer_blit(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 5);
    JanetBuffer *dest = janet_getbuffer(argv, 0);
    JanetByteView src = janet_getbytes(argv, 1);
    int same_buf = src.bytes == dest->data;
    int32_t offset_dest = 0;
    int32_t offset_src = 0;
    if (argc > 2)
        offset_dest = janet_gethalfrange(argv, 2, dest->count, "dest-start");
    if (argc > 3)
        offset_src = janet_gethalfrange(argv, 3, src.len, "src-start");
    int32_t length_src;
    if (argc > 4) {
        int32_t src_end = janet_gethalfrange(argv, 4, src.len, "src-end");
        length_src = src_end - offset_src;
        if (length_src < 0) length_src = 0;
    } else {
        length_src = src.len - offset_src;
    }
    int64_t last = (int64_t) offset_dest + length_src;
    if (last > INT32_MAX)
        janet_panic("buffer blit out of range");
    int32_t last32 = (int32_t) last;
    janet_buffer_ensure(dest, last32, 2);
    if (last32 > dest->count) dest->count = last32;
    if (length_src) {
        if (same_buf) {
            /* janet_buffer_ensure may have invalidated src */
            src.bytes = dest->data;
            memmove(dest->data + offset_dest, src.bytes + offset_src, length_src);
        } else {
            memcpy(dest->data + offset_dest, src.bytes + offset_src, length_src);
        }
    }
    return argv[0];
}

static Janet cfun_buffer_format(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, -1);
    JanetBuffer *buffer = janet_getbuffer(argv, 0);
    const char *strfrmt = (const char *) janet_getstring(argv, 1);
    janet_buffer_format(buffer, strfrmt, 1, argc, argv);
    return argv[0];
}

static const JanetReg buffer_cfuns[] = {
    {
        "buffer/new", cfun_buffer_new,
        JDOC("(buffer/new capacity)\n\n"
             "Creates a new, empty buffer with enough backing memory for capacity bytes. "
             "Returns a new buffer of length 0.")
    },
    {
        "buffer/new-filled", cfun_buffer_new_filled,
        JDOC("(buffer/new-filled count &opt byte)\n\n"
             "Creates a new buffer of length count filled with byte. By default, byte is 0. "
             "Returns the new buffer.")
    },
    {
        "buffer/fill", cfun_buffer_fill,
        JDOC("(buffer/fill buffer &opt byte)\n\n"
             "Fill up a buffer with bytes, defaulting to 0s. Does not change the buffer's length. "
             "Returns the modified buffer.")
    },
    {
        "buffer/trim", cfun_buffer_trim,
        JDOC("(buffer/trim buffer)\n\n"
             "Set the backing capacity of the buffer to the current length of the buffer. Returns the "
             "modified buffer.")
    },
    {
        "buffer/push-byte", cfun_buffer_u8,
        JDOC("(buffer/push-byte buffer x)\n\n"
             "Append a byte to a buffer. Will expand the buffer as necessary. "
             "Returns the modified buffer. Will throw an error if the buffer overflows.")
    },
    {
        "buffer/push-word", cfun_buffer_word,
        JDOC("(buffer/push-word buffer x)\n\n"
             "Append a machine word to a buffer. The 4 bytes of the integer are appended "
             "in twos complement, little endian order, unsigned. Returns the modified buffer. Will "
             "throw an error if the buffer overflows.")
    },
    {
        "buffer/push-string", cfun_buffer_chars,
        JDOC("(buffer/push-string buffer str)\n\n"
             "Push a string onto the end of a buffer. Non string values will be converted "
             "to strings before being pushed. Returns the modified buffer. "
             "Will throw an error if the buffer overflows.")
    },
    {
        "buffer/popn", cfun_buffer_popn,
        JDOC("(buffer/popn buffer n)\n\n"
             "Removes the last n bytes from the buffer. Returns the modified buffer.")
    },
    {
        "buffer/clear", cfun_buffer_clear,
        JDOC("(buffer/clear buffer)\n\n"
             "Sets the size of a buffer to 0 and empties it. The buffer retains "
             "its memory so it can be efficiently refilled. Returns the modified buffer.")
    },
    {
        "buffer/slice", cfun_buffer_slice,
        JDOC("(buffer/slice bytes &opt start end)\n\n"
             "Takes a slice of a byte sequence from start to end. The range is half open, "
             "[start, end). Indexes can also be negative, indicating indexing from the end of the "
             "end of the array. By default, start is 0 and end is the length of the buffer. "
             "Returns a new buffer.")
    },
    {
        "buffer/bit-set", cfun_buffer_bitset,
        JDOC("(buffer/bit-set buffer index)\n\n"
             "Sets the bit at the given bit-index. Returns the buffer.")
    },
    {
        "buffer/bit-clear", cfun_buffer_bitclear,
        JDOC("(buffer/bit-clear buffer index)\n\n"
             "Clears the bit at the given bit-index. Returns the buffer.")
    },
    {
        "buffer/bit", cfun_buffer_bitget,
        JDOC("(buffer/bit buffer index)\n\n"
             "Gets the bit at the given bit-index. Returns true if the bit is set, false if not.")
    },
    {
        "buffer/bit-toggle", cfun_buffer_bittoggle,
        JDOC("(buffer/bit-toggle buffer index)\n\n"
             "Toggles the bit at the given bit index in buffer. Returns the buffer.")
    },
    {
        "buffer/blit", cfun_buffer_blit,
        JDOC("(buffer/blit dest src &opt dest-start src-start src-end)\n\n"
             "Insert the contents of src into dest. Can optionally take indices that "
             "indicate which part of src to copy into which part of dest. Indices can be "
             "negative to index from the end of src or dest. Returns dest.")
    },
    {
        "buffer/format", cfun_buffer_format,
        JDOC("(buffer/format buffer format & args)\n\n"
             "Snprintf like functionality for printing values into a buffer. Returns "
             " the modified buffer.")
    },
    {NULL, NULL, NULL}
};

void janet_lib_buffer(JanetTable *env) {
    janet_core_cfuns(env, NULL, buffer_cfuns);
}
