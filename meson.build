# Copyright (c) 2019 Calvin Rose and contributors
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

project('janet', 'c', default_options : ['c_std=c99'])

# Link math library on all systems
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)
dl_dep = cc.find_library('dl', required : false)

# Some options
add_project_arguments('-DJANET_BUILD="meson"', language : 'c')

# Include directories
incdir = include_directories('src/include')

# Building generated sources
xxd = executable('xxd', 'tools/xxd.c')
gen = generator(xxd,
  output : '@BASENAME@.gen.c',
  arguments : ['@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@'])
core_gen = gen.process('src/core/core.janet', extra_args: 'janet_gen_core')
boot_gen = gen.process('src/boot/boot.janet', extra_args: 'janet_gen_boot')
init_gen = gen.process('src/mainclient/init.janet', extra_args: 'janet_gen_init')

core_src = [
  'src/core/abstract.c',
  'src/core/array.c',
  'src/core/asm.c',
  'src/core/buffer.c',
  'src/core/bytecode.c',
  'src/core/capi.c',
  'src/core/cfuns.c',
  'src/core/compile.c',
  'src/core/corelib.c',
  'src/core/debug.c',
  'src/core/emit.c',
  'src/core/fiber.c',
  'src/core/gc.c',
  'src/core/inttypes.c',
  'src/core/io.c',
  'src/core/marsh.c',
  'src/core/math.c',
  'src/core/os.c',
  'src/core/parse.c',
  'src/core/peg.c',
  'src/core/pp.c',
  'src/core/regalloc.c',
  'src/core/run.c',
  'src/core/specials.c',
  'src/core/string.c',
  'src/core/strtod.c',
  'src/core/struct.c',
  'src/core/symcache.c',
  'src/core/table.c',
  'src/core/tuple.c',
  'src/core/typedarray.c',
  'src/core/util.c',
  'src/core/value.c',
  'src/core/vector.c',
  'src/core/vm.c',
  'src/core/wrap.c'
]

boot_src = [
  'src/boot/array_test.c',
  'src/boot/boot.c',
  'src/boot/buffer_test.c',
  'src/boot/number_test.c',
  'src/boot/system_test.c',
  'src/boot/table_test.c',
]

mainclient_src = [
  'src/mainclient/line.c',
  'src/mainclient/main.c'
]

# Build boot binary
janet_boot = executable('janet_boot', core_src, boot_src, core_gen, boot_gen,
  c_args : '-DJANET_BOOTSTRAP',
  dependencies : [m_dep, dl_dep])

# Build core image
core_image = custom_target('core_image',
  input : [janet_boot],
  output : 'core_image.gen.c',
  command : [janet_boot, '@OUTPUT@',
    'JANET_PATH', join_paths(get_option('libdir'), 'janet')])

libjanet = shared_library('janet', core_src, core_image,
  dependencies : [m_dep, dl_dep],
  install : true)
janet_mainclient = executable('janet', core_src, core_image, init_gen, mainclient_src,
  dependencies : [m_dep, dl_dep],
  install : true)

# Tests
test_files = [
  'test/suite0.janet',
  'test/suite1.janet',
  'test/suite2.janet',
  'test/suite3.janet',
  'test/suite4.janet',
  'test/suite5.janet',
  'test/suite6.janet'
]
foreach t : test_files
  test(t, janet_mainclient, args : files([t]), workdir : meson.current_source_dir())
endforeach

# Repl
run_target('repl', command : [janet_mainclient])

# Installation
install_man('janet.1')
install_headers('src/include/janet.h', 'src/include/janetconf.h')
janet_libs = [
  'src/include/janet.h',
  'src/include/janetconf.h'
  'tools/bars.janet',
  'tools/cook.janet',
  'tools/highlight.janet'
]
install_data(sources : janet_libs)
