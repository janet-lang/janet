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
#include "internal.h"

/**
 * Data format v1
 *
 * Types:
 *
 * Byte 0 to 200: small integer with value (byte - 100)
 * Byte 201: Nil
 * Byte 202: True
 * Byte 203: False
 * Byte 204: Number  - IEEE 754 double format
 * Byte 205: String  - [u32 length]*[u8... characters]
 * Byte 206: Struct  - [u32 length]*2*[value... kvs]
 * Byte 207: Buffer  - [u32 capacity][u32 length]*[u8... characters]
 * Byte 208: Array   - [u32 length]*[value... elements]
 * Byte 209: Tuple   - [u32 length]*[value... elements]
 * Byte 210: Thread  - [value parent][u8 state][u32 frames]*[[value callee][value env]
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
 *
 * This data format needs both a serialization function and deserialization function
 * written in C, as this will load embeded and precompiled programs into the VM, including
 * the self hosted parser abd compiler. It is therefor important that it is correct,
 * does not leak memory, and is fast.
 */
