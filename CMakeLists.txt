# Copyright (c) 2017 Calvin Rose
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.9)
project(dst)

# Set Some Variables
set(TARGET_NAME ${PROJECT_NAME})
if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")
    endif ()
else ()
    set (CMAKE_C_STANDARD 99)
endif ()

include_directories(src/include)

set(ASSEMBLER_SOURCES
src/assembler/asm.c
)

set(COMPILER_SOURCES
src/compiler/compile.c
src/compiler/compile_specials.c
src/compiler/context.c

src/compiler/compile.h
)

set(CORE_SOURCES
src/core/abstract.c
src/core/array.c
src/core/ast.c
src/core/buffer.c
src/core/bytecode.c
src/core/fiber.c
src/core/gc.c
src/core/io.c
src/core/math.c
src/core/native.c
src/core/stl.c
src/core/string.c
src/core/struct.c
src/core/symcache.c
src/core/table.c
src/core/tuple.c
src/core/util.c
src/core/value.c
src/core/vm.c
src/core/wrap.c

src/core/gc.h
src/core/symcache.h
src/core/util.h
)

set(MAINCLIENT_SOURCES
src/mainclient/main.c
)

set(PARSER_SOURCES
src/parser/parse.c
src/parser/strtod.c
)

set(TESTLIB_SOURCES
src/testlib/testlib.c
)

set(SOURCES
${ASSEMBLER_SOURCES}
${COMPILER_SOURCES}
${CORE_SOURCES}
${MAINCLIENT_SOURCES}
${PARSER_SOURCES}
)

# Build the executable
add_executable(${TARGET_NAME} ${SOURCES})
target_link_libraries(${TARGET_NAME} m dl)
