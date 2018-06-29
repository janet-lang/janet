# Copyright (c) 2018 Calvin Rose
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

cmake_minimum_required(VERSION 3.7)
project(dst)

# Set Some Variables
set(TARGET_NAME ${PROJECT_NAME})
set (CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

# Set configurations
SET(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")
SET(CMAKE_C_FLAGS_RELEASE "-O2")
SET(CMAKE_C_FLAGS_DEBUG  "-O0 -g")

include_directories(src/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(ASSEMBLER_SOURCES
src/assembler/asm.c
)

set(COMPILER_SOURCES
src/compiler/compile.c
src/compiler/cfuns.c
src/compiler/run.c
src/compiler/specials.c
src/compiler/stl.c

dststlbootstrap.h
src/compiler/compile.h
)

set(CORE_SOURCES
src/core/abstract.c
src/core/array.c
src/core/buffer.c
src/core/bytecode.c
src/core/corelib.c
src/core/fiber.c
src/core/gc.c
src/core/io.c
src/core/marsh.c
src/core/math.c
src/core/native.c
src/core/os.c
src/core/parse.c
src/core/string.c
src/core/strtod.c
src/core/struct.c
src/core/symcache.c
src/core/table.c
src/core/tuple.c
src/core/util.c
src/core/value.c
src/core/vm.c
src/core/wrap.c

src/core/fiber.h
src/core/gc.h
src/core/state.h
src/core/symcache.h
src/core/util.h
)

set(MAINCLIENT_SOURCES
clientinit.h
src/mainclient/main.c
src/mainclient/line.c
src/mainclient/line.h
)

set(TESTLIB_SOURCES
src/testlib/testlib.c
)

set(REPL_SOURCES
${ASSEMBLER_SOURCES}
${COMPILER_SOURCES}
${CORE_SOURCES}
${MAINCLIENT_SOURCES}
)

# Set Public headers
set(DST_PUBLIC_HEADERS
src/include/dst/dst.h
src/include/dst/dstasm.h
src/include/dst/dstcompile.h
src/include/dst/dstconfig.h
src/include/dst/dstcorelib.h
src/include/dst/dstopcodes.h
src/include/dst/dsttypes.h
)

# Build the executable
add_executable(${TARGET_NAME} ${REPL_SOURCES})
if (APPLE)
    # macOS flags here
elseif (UNIX)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -rdynamic")
	target_link_libraries(${TARGET_NAME} m dl)
endif (APPLE)
set_target_properties(${TARGET_NAME} PROPERTIES PUBLIC_HEADER "${DST_PUBLIC_HEADERS}")

# Generate header containing standard library
add_custom_command(
    OUTPUT dststlbootstrap.h
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dststlbootstrap.cmake
    DEPENDS src/compiler/boot.dst
    COMMENT "Generating stl bootstrap C header for embedding"
)

# Generate header containing main client script
add_custom_command(
    OUTPUT clientinit.h
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dstmainclientinit.cmake
    DEPENDS src/mainclient/init.dst
    COMMENT "Generating mainclient init C header for embedding"
)

# Install
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION "lib"
        RUNTIME DESTINATION "bin"
        PUBLIC_HEADER DESTINATION "include/dst"
)

# Add some test scripts
enable_testing()
add_test(NAME suite0 COMMAND ${TARGET_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/test/suite0.dst
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME suite1 COMMAND ${TARGET_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/test/suite1.dst
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Add convenience script to run repl
add_custom_target(run
    COMMAND ${TARGET_NAME}
    DEPENDS ${TARGET_NAME}
    WORKING_DIRECTORY ${CMAKE_PROJECT_DIR}
)
