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

#include "internal.h"
#include <dst/dst.h>

/* Initialize a buffer */
DstBuffer *dst_buffer(Dst *vm, uint32_t capacity) {
    DstBuffer *buffer = dst_alloc(vm, DST_MEMORY_BUFFER, sizeof(DstBuffer));
    uint8_t *data = malloc(sizeof(uint8_t) * capacity);
    if (NULL == data) {
        DST_OUT_OF_MEMORY;
    }
    buffer->count = 0;
    buffer->capacity = capacity;
    return buffer;
}

/* Ensure that the buffer has enough internal capacity */
void dst_buffer_ensure(Dst *vm, DstBuffer *buffer, uint32_t capacity) {
    uint8_t *newData;
    uint8_t *old = buffer->data;
    if (capacity <= buffer->capacity) return;
    newData = realloc(old, capacity * sizeof(uint8_t));
    if (NULL == newData) {
        DST_OUT_OF_MEMORY;
    }
    buffer->data = newData;
    buffer->capacity = capacity;
}

/* Adds capacity for enough extra bytes to the buffer. Ensures that the
 * next n bytes pushed to the buffer will not cause a reallocation */
void dst_buffer_extra(Dst *vm, DstBuffer *buffer, uint32_t n) {
    uint32_t newCount = buffer->count + n;
    if (newCount > buffer->capacity) {
        uint32_t newCapacity = newCount * 2;
        uint8_t *newData = realloc(buffer->data, newCapacity * sizeof(uint8_t));
        if (NULL == newData) {
            DST_OUT_OF_MEMORY;
        }
        buffer->data = newData;
        buffer->capacity = newCapacity;
    }
}

/* Push multiple bytes into the buffer */
void dst_buffer_push_bytes(Dst *vm, DstBuffer *buffer, const uint8_t *string, uint32_t length) {
    uint32_t newSize = buffer->count + length;
    if (newSize > buffer->capacity) {
        dst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    dst_memcpy(buffer->data + buffer->count, string, length);
    buffer->count = newSize;
}

/* Push a cstring to buffer */
void dst_buffer_push_cstring(Dst *vm, DstBuffer *buffer, const char *cstring) {
    uint32_t len = 0;
    while (cstring[len]) ++len;
    dst_buffer_push_bytes(vm, buffer, (const uint8_t *) cstring, len);
}

/* Push a single byte to the buffer */
void dst_buffer_push_u8(Dst *vm, DstBuffer *buffer, uint8_t byte) {
    uint32_t newSize = buffer->count + 1;
    if (newSize > buffer->capacity) {
        dst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    buffer->data[buffer->count] = byte;
    buffer->count = newSize;
}

/* Push a 16 bit unsigned integer to the buffer */
void dst_buffer_push_u16(Dst *vm, DstBuffer *buffer, uint16_t x) {
    uint32_t newSize = buffer->count + 2;
    if (newSize > buffer->capacity) {
        dst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->count = newSize;
}

/* Push a 32 bit unsigned integer to the buffer */
void dst_buffer_push_u32(Dst *vm, DstBuffer *buffer, uint32_t x) {
    uint32_t newSize = buffer->count + 4;
    if (newSize > buffer->capacity) {
        dst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->data[buffer->count + 2] = (x >> 16) & 0xFF;
    buffer->data[buffer->count + 3] = (x >> 24) & 0xFF;
    buffer->count = newSize;
}

/* Push a 64 bit unsigned integer to the buffer */
void dst_buffer_push_u64(Dst *vm, DstBuffer *buffer, uint64_t x) {
    uint32_t newSize = buffer->count + 8;
    if (newSize > buffer->capacity) {
        dst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->data[buffer->count + 2] = (x >> 16) & 0xFF;
    buffer->data[buffer->count + 3] = (x >> 24) & 0xFF;
    buffer->data[buffer->count + 4] = (x >> 32) & 0xFF;
    buffer->data[buffer->count + 5] = (x >> 40) & 0xFF;
    buffer->data[buffer->count + 6] = (x >> 48) & 0xFF;
    buffer->data[buffer->count + 7] = (x >> 56) & 0xFF;
    buffer->count = newSize;
}
