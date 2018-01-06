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

#include <dst/dst.h>
#include "gc.h"

/* Initialize a buffer */
DstBuffer *dst_buffer_init(DstBuffer *buffer, int32_t capacity) {
    uint8_t *data = NULL;
    if (capacity > 0) {
        data = malloc(sizeof(uint8_t) * capacity);
        if (NULL == data) {
            DST_OUT_OF_MEMORY;
        }
    }
    buffer->count = 0;
    buffer->capacity = capacity;
    buffer->data = data;
    return buffer;
}

/* Deinitialize a buffer (free data memory) */
void dst_buffer_deinit(DstBuffer *buffer) {
    free(buffer->data);
}

/* Initialize a buffer */
DstBuffer *dst_buffer(int32_t capacity) {
    DstBuffer *buffer = dst_gcalloc(DST_MEMORY_BUFFER, sizeof(DstBuffer));
    return dst_buffer_init(buffer, capacity);
}

/* Ensure that the buffer has enough internal capacity */
void dst_buffer_ensure(DstBuffer *buffer, int32_t capacity) {
    uint8_t *new_data;
    uint8_t *old = buffer->data;
    if (capacity <= buffer->capacity) return;
    new_data = realloc(old, capacity * sizeof(uint8_t));
    if (NULL == new_data) {
        DST_OUT_OF_MEMORY;
    }
    buffer->data = new_data;
    buffer->capacity = capacity;
}

/* Adds capacity for enough extra bytes to the buffer. Ensures that the
 * next n bytes pushed to the buffer will not cause a reallocation */
int dst_buffer_extra(DstBuffer *buffer, int32_t n) {
    /* Check for buffer overflow */
    if ((int64_t)n + buffer->count > INT32_MAX) {
        return -1;
    }
    int32_t new_size = buffer->count + n;
    if (new_size > buffer->capacity) {
        int32_t new_capacity = new_size * 2;
        uint8_t *new_data = realloc(buffer->data, new_capacity * sizeof(uint8_t));
        if (NULL == new_data) {
            DST_OUT_OF_MEMORY;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    return 0;
}

/* Push a cstring to buffer */
int dst_buffer_push_cstring(DstBuffer *buffer, const char *cstring) {
    int32_t len = 0;
    while (cstring[len]) ++len;
    return dst_buffer_push_bytes(buffer, (const uint8_t *) cstring, len);
}

/* Push multiple bytes into the buffer */
int dst_buffer_push_bytes(DstBuffer *buffer, const uint8_t *string, int32_t length) {
    if (dst_buffer_extra(buffer, length)) return -1;
    memcpy(buffer->data + buffer->count, string, length);
    buffer->count += length;
    return 0;
}

/* Push a single byte to the buffer */
int dst_buffer_push_u8(DstBuffer *buffer, uint8_t byte) {
    if (dst_buffer_extra(buffer, 1)) return -1;
    buffer->data[buffer->count] = byte;
    buffer->count++;
    return 0;
}

/* Push a 16 bit unsigned integer to the buffer */
int dst_buffer_push_u16(DstBuffer *buffer, uint16_t x) {
    if (dst_buffer_extra(buffer, 2)) return -1;
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->count += 2;
    return 0;
}

/* Push a 32 bit unsigned integer to the buffer */
int dst_buffer_push_u32(DstBuffer *buffer, uint32_t x) {
    if (dst_buffer_extra(buffer, 4)) return -1;
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->data[buffer->count + 2] = (x >> 16) & 0xFF;
    buffer->data[buffer->count + 3] = (x >> 24) & 0xFF;
    buffer->count += 4;
    return 0;
}

/* Push a 64 bit unsigned integer to the buffer */
int dst_buffer_push_u64(DstBuffer *buffer, uint64_t x) {
    if (dst_buffer_extra(buffer, 8)) return -1;
    buffer->data[buffer->count] = x & 0xFF;
    buffer->data[buffer->count + 1] = (x >> 8) & 0xFF;
    buffer->data[buffer->count + 2] = (x >> 16) & 0xFF;
    buffer->data[buffer->count + 3] = (x >> 24) & 0xFF;
    buffer->data[buffer->count + 4] = (x >> 32) & 0xFF;
    buffer->data[buffer->count + 5] = (x >> 40) & 0xFF;
    buffer->data[buffer->count + 6] = (x >> 48) & 0xFF;
    buffer->data[buffer->count + 7] = (x >> 56) & 0xFF;
    buffer->count += 8;
    return 0;
}
