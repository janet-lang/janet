#include <string.h>
#include "buffer.h"
#include "gc.h"
#include "value.h"
#include "vstring.h"

Buffer * BufferNew(GC * gc, uint32_t capacity) {
    Buffer * buffer = GCAlloc(gc, sizeof(Buffer));
    uint8_t * data = GCAlloc(gc, sizeof(uint8_t) * capacity);
    buffer->data = data;
    buffer->count = 0;
    buffer->capacity = capacity;
    return buffer;
}

void BufferEnsure(GC * gc, Buffer * buffer, uint32_t capacity) {
    uint8_t * newData;
    if (capacity <= buffer->capacity) return;
    newData = GCAlloc(gc, capacity * sizeof(uint8_t));
    memcpy(newData, buffer->data, buffer->count * sizeof(uint8_t));
    buffer->data = newData;
    buffer->capacity = capacity;
}

int32_t BufferGet(Buffer * buffer, uint32_t index) {
    if (index < buffer->count) {
        return buffer->data[index];
    } else {
        return -1;
    }
}

void BufferPush(GC * gc, Buffer * buffer, uint8_t c) {
    if (buffer->count >= buffer->capacity) {
        BufferEnsure(gc, buffer, 2 * buffer->count);
    }
    buffer->data[buffer->count++] = c;
}

void BufferAppendData(GC * gc, Buffer * buffer, uint8_t * string, uint32_t length) {
    uint32_t newSize = buffer->count + length;
    if (newSize > buffer->capacity) {
        BufferEnsure(gc, buffer, 2 * newSize);
    }
    memcpy(buffer->data + buffer->count, string, length);
    buffer->count = newSize;
}

uint8_t * BufferToString(GC * gc, Buffer * buffer) {
    uint8_t * data = GCAlloc(gc, buffer->count + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    VStringSize(data) = buffer->count;
    VStringHash(data) = 0;
    memcpy(data, buffer->data, buffer->count * sizeof(uint8_t));
    return data;
}
