# Copyright (c) 2020 Calvin Rose and contributors
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

project('janet', 'c',
  default_options : ['c_std=c99', 'b_lundef=false', 'default_library=both'],
  version : '1.13.0')

# Global settings
janet_path = join_paths(get_option('prefix'), get_option('libdir'), 'janet')
header_path = join_paths(get_option('prefix'), get_option('includedir'), 'janet')

# Link math library on all systems
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)
dl_dep = cc.find_library('dl', required : false)
thread_dep = dependency('threads')

# Link options
if build_machine.system() != 'windows'
    add_project_link_arguments('-rdynamic', language : 'c')
endif

# Generate custom janetconf.h
conf = configuration_data()
version_parts = meson.project_version().split('.')
last_parts = version_parts[2].split('-')
if last_parts.length() > 1
  conf.set_quoted('JANET_VERSION_EXTRA', '-' + last_parts[1])
else
  conf.set_quoted('JANET_VERSION_EXTRA', '')
endif
conf.set('JANET_VERSION_MAJOR', version_parts[0].to_int())
conf.set('JANET_VERSION_MINOR', version_parts[1].to_int())
conf.set('JANET_VERSION_PATCH', last_parts[0].to_int())
conf.set_quoted('JANET_VERSION', meson.project_version())
# Use options
conf.set_quoted('JANET_BUILD', get_option('git_hash'))
conf.set('JANET_NO_NANBOX', not get_option('nanbox'))
conf.set('JANET_SINGLE_THREADED', get_option('single_threaded'))
conf.set('JANET_NO_DYNAMIC_MODULES', not get_option('dynamic_modules'))
conf.set('JANET_NO_DOCSTRINGS', not get_option('docstrings'))
conf.set('JANET_NO_SOURCEMAPS', not get_option('sourcemaps'))
conf.set('JANET_NO_ASSEMBLER', not get_option('assembler'))
conf.set('JANET_NO_PEG', not get_option('peg'))
conf.set('JANET_NO_NET', not get_option('net'))
conf.set('JANET_REDUCED_OS', get_option('reduced_os'))
conf.set('JANET_NO_TYPED_ARRAY', not get_option('typed_array'))
conf.set('JANET_NO_INT_TYPES', not get_option('int_types'))
conf.set('JANET_PRF', get_option('prf'))
conf.set('JANET_RECURSION_GUARD', get_option('recursion_guard'))
conf.set('JANET_MAX_PROTO_DEPTH', get_option('max_proto_depth'))
conf.set('JANET_MAX_MACRO_EXPAND', get_option('max_macro_expand'))
conf.set('JANET_STACK_MAX', get_option('stack_max'))
conf.set('JANET_NO_UMASK', not get_option('umask'))
conf.set('JANET_NO_REALPATH', not get_option('realpath'))
conf.set('JANET_NO_PROCESSES', not get_option('processes'))
conf.set('JANET_SIMPLE_GETLINE', get_option('simple_getline'))
if get_option('os_name') != ''
  conf.set('JANET_OS_NAME', get_option('os_name'))
endif
if get_option('arch_name') != ''
  conf.set('JANET_ARCH_NAME', get_option('arch_name'))
endif
jconf = configure_file(output : 'janetconf.h',
  configuration : conf)

# Include directories
incdir = include_directories(['src/include', '.'])

# Order is important here, as some headers
# depend on other headers for the amalg target
core_headers = [
  'src/core/features.h',
  'src/core/util.h',
  'src/core/state.h',
  'src/core/gc.h',
  'src/core/vector.h',
  'src/core/fiber.h',
  'src/core/regalloc.h',
  'src/core/compile.h',
  'src/core/emit.h',
  'src/core/symcache.h'
]

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
  'src/core/net.c',
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
  'src/core/thread.c',
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
  'src/mainclient/shell.c'
]

# Build boot binary
janet_boot = executable('janet-boot', core_src, boot_src,
  include_directories : incdir,
  c_args : '-DJANET_BOOTSTRAP',
  dependencies : [m_dep, dl_dep, thread_dep],
  native : true)

# Build janet.c
janetc = custom_target('janetc',
  input : [janet_boot],
  output : 'janet.c',
  capture : true,
  command : [
    janet_boot, meson.current_source_dir(),
    'JANET_PATH', janet_path, 'JANET_HEADERPATH', header_path
  ])

libjanet = library('janet', janetc,
  include_directories : incdir,
  dependencies : [m_dep, dl_dep, thread_dep],
  version: meson.project_version(),
  soversion: version_parts[0] + '.' + version_parts[1],
  install : true)

# Extra c flags - adding -fvisibility=hidden matches the Makefile and
# shaves off about 10k on linux x64, likely similar on other platforms.
if cc.has_argument('-fvisibility=hidden')
  extra_cflags = ['-fvisibility=hidden']
else
  extra_cflags = []
endif
janet_mainclient = executable('janet', janetc, mainclient_src,
  include_directories : incdir,
  dependencies : [m_dep, dl_dep, thread_dep],
  c_args : extra_cflags,
  install : true)

if meson.is_cross_build()
  native_cc = meson.get_compiler('c', native: true)
  if native_cc.has_argument('-fvisibility=hidden')
    extra_native_cflags = ['-fvisibility=hidden']
  else
    extra_native_cflags = []
  endif
  janet_nativeclient = executable('janet-native', janetc, mainclient_src,
    include_directories : incdir,
    dependencies : [m_dep, dl_dep, thread_dep],
    c_args : extra_native_cflags,
    native : true)
else
  janet_nativeclient = janet_mainclient
endif

# Documentation
docs = custom_target('docs',
  input : ['tools/gendoc.janet'],
  output : ['doc.html'],
  capture : true,
  command : [janet_nativeclient, '@INPUT@'])

# Tests
test_files = [
  'test/suite0000.janet',
  'test/suite0001.janet',
  'test/suite0002.janet',
  'test/suite0003.janet',
  'test/suite0004.janet',
  'test/suite0005.janet',
  'test/suite0006.janet',
  'test/suite0007.janet',
  'test/suite0008.janet',
  'test/suite0009.janet',
  'test/suite0010.janet'
]
foreach t : test_files
  test(t, janet_nativeclient, args : files([t]), workdir : meson.current_source_dir())
endforeach

# Repl
run_target('repl', command : [janet_nativeclient])

# For use as meson subproject (wrap)
janet_dep = declare_dependency(include_directories : incdir,
  link_with : libjanet)

# pkgconfig
pkg = import('pkgconfig')
pkg.generate(libjanet,
  description: 'Library for the Janet programming language.')

# Installation
install_man('janet.1')
install_headers(['src/include/janet.h', jconf], subdir: 'janet')
install_data(sources : ['tools/.keep'], install_dir : join_paths(get_option('libdir'), 'janet'))
if get_option('peg') and not get_option('reduced_os') and get_option('processes')
  install_man('jpm.1')
  patched_jpm = custom_target('patched-jpm',
    input : ['tools/patch-jpm.janet', 'jpm'],
    install : true,
    install_dir : get_option('bindir'),
    build_by_default : true,
    output : ['jpm'],
    command : [janet_nativeclient, '@INPUT@', '@OUTPUT@',
      '--binpath=' + join_paths(get_option('prefix'), get_option('bindir')),
      '--libpath=' + join_paths(get_option('prefix'), get_option('libdir')),
      '--headerpath=' + join_paths(get_option('prefix'), get_option('includedir'))])
endif
