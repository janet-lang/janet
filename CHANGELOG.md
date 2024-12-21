# Changelog
All notable changes to this project will be documented in this file.

## ??? - Unreleased
- Add `struct/rawget`
- Fix `deep=` and `deep-not=` to better handle degenerate cases with mutable table keys
- Long strings will now dedent on `\r\n` instead of just `\n`.
- Add `ev/to-file` for synchronous resource operations

## 1.37.1 - 2024-12-05
- Fix meson cross compilation
- Update timeout documentation for networking APIs: timeouts raise errors and do not return nil.
- Add `janet_addtimeout_nil(double sec);` to the C API.
- Change string hashing.
- Fix string equality bug.
- Add `assertf`
- Change how JANET_PROFILE is loaded to allow more easily customizing the environment.
- Add `*repl-prompt*` dynamic binding to allow customizing the built in repl.
- Add multiple path support in the `JANET_PATH` environment variables. This lets
  user more easily import modules from many directories.
- Add `nth` and `only-tags` PEG specials to select from sub-captures while
  dropping the rest.

## 1.36.0 - 2024-09-07
- Improve error messages in `bundle/add*` functions.
- Add CI testing and verify tests pass on the s390x architecture.
- Save `:source-form` in environment entries when `*debug*` is set.
- Add experimental `filewatch/` module for listening to file system changes on Linux and Windows.
- Add `bundle/who-is` to query which bundle a file on disk was installed by.
- Add `geomean` function
- Add `:R` and `:W` flags to `os/pipe` to create blocking pipes on Posix and Windows systems.
  These streams cannot be directly read to and written from, but can be passed to subprocesses.
- Add `array/join`
- Add `tuple/join`
- Add `bundle/add-bin` to make installing scripts easier. This also establishes a packaging convention for it.
- Fix marshalling weak tables and weak arrays.
- Fix bug in `ev/` module that could accidentally close sockets on accident.
- Expose C functions for constructing weak tables in janet.h
- Let range take non-integer values.

## 1.35.2 - 2024-06-16
- Fix some documentation typos.
- Allow using `:only` in import without quoting.

## 1.35.0 - 2024-06-15
- Add `:only` argument to `import` to allow for easier control over imported bindings.
- Add extra optional `env` argument to `eval` and `eval-string`.
- Allow naming function literals with a keyword. This allows better stacktraces for macros without
  accidentally adding new bindings.
- Add `bundle/` module for managing packages within Janet. This should replace the jpm packaging
  format eventually and is much simpler and amenable to more complicated builds.
- Add macros `ev/with-lock`, `ev/with-rlock`, and `ev/with-wlock` for using mutexes and rwlocks.
- Add `with-env`
- Add *module-make-env* dynamic binding
- Add buffer/format-at
- Add long form command line options for readable CLI usage
- Fix bug with `net/accept-loop` that would sometimes miss connections.

## 1.34.0 - 2024-03-22
- Add a new (split) PEG special by @ianthehenry
- Add buffer/push-* sized int and float by @pnelson
- Documentation improvements: @amano-kenji, @llmII, @MaxGyver83, @pepe, @sogaiu.
- Expose _exit to skip certain cleanup with os/exit.
- Swap set / body order for each by @sogaiu.
- Abort on assert failure instead of exit.
- Fix: os/proc-wait by @llmII.
- Fix macex1 to keep syntax location for all tuples.
- Restore if-let tail calls.
- Don't try and resume fibers that can't be resumed.
- Register stream on unmarshal.
- Fix asm roundtrip issue.

## 1.33.0 - 2024-01-07
- Add more + and * keywords to default-peg-grammar by @sogaiu.
- Use libc strlen in janet_buffer_push_cstring by @williewillus.
- Be a bit safer with reference counting.
- Add support for atomic loads in Janet's atomic abstraction.
- Fix poll event loop CPU usage issue.
- Add ipv6, shared, and cryptorand options to meson.
- Add more ipv6 feature detection.
- Fix loop for forever loop.
- Cleaned up unused NetStateConnect, fixed janet_async_end() ev refcount by @zevv.
- Fix warnings w/ MSVC and format.
- Fix marshal_one_env w/ JANET_MARSHAL_UNSAFE.
- Fix `(default)`.
- Fix cannot marshal fiber with c stackframe, in a dynamic way that is fairly conservative.
- Fix typo for SIGALARM in os/proc-kill.
- Prevent bytecode optimization from remove mk* instructions.
- Fix arity typo in peg.c by @pepe.
- Update Makefile for MinGW.
- Fix canceling waiting fiber.
- Add a new (sub) PEG special by @ianthehenry.
- Fix if net/server's handler has incorrect arity.
- Fix macex raising on ().

## 1.32.1 - 2023-10-15
- Fix return value from C function `janet_dobytes` when called on Janet functions that yield to event loop.
- Change C API for event loop interaction - get rid of JanetListener and instead use `janet_async_start` and `janet_async_end`.
- Rework event loop to make fewer system calls on kqueue and epoll.
- Expose atomic refcount abstraction in janet.h
- Add `array/weak` for weak references in arrays
- Add support for weak tables via `table/weak`, `table/weak-keys`, and `table/weak-values`.
- Fix compiler bug with using the result of `(break x)` expression in some contexts.
- Rework internal event loop code to be better behaved on Windows
- Update meson build to work better on windows

## 1.31.0 - 2023-09-17
- Report line and column when using `janet_dobytes`
- Add `:unless` loop modifier
- Allow calling `reverse` on generators.
- Improve performance of a number of core functions including `partition`, `mean`, `keys`, `values`, `pairs`, `interleave`.
- Add `lengthable?`
- Add `os/sigaction`
- Change `every?` and `any?` to behave like the functional versions of the `and` and `or` macros.
- Fix bug with garbage collecting threaded abstract types.
- Add `:signal` to the `sandbox` function to allow intercepting signals.

## 1.30.0 - 2023-08-05
- Change indexing of `array/remove` to start from -1 at the end instead of -2.
- Add new string escape sequences `\\a`, `\\b`, `\\?`, and `\\'`.
- Fix bug with marshalling channels
- Add `div` for floored division
- Make `div` and `mod` variadic
- Support `bnot` for integer types.
- Define `(mod x 0)` as `x`
- Add `ffi/pointer-cfunction` to convert pointers to cfunctions

## 1.29.1 - 2023-06-19
- Add support for passing booleans to PEGs for "always" and "never" matching.
- Allow dictionary types for `take` and `drop`
- Fix bug with closing channels while other fibers were waiting on them - `ev/take`, `ev/give`, and `ev/select`  will now return the correct (documented) value when another fiber closes the channel.
- Add `ffi/calling-conventions` to show all available calling conventions for FFI.
- Add `net/setsockopt`
- Add `signal` argument to `os/proc-kill` to send signals besides `SIGKILL` on Posix.
- Add `source` argument to `os/clock` to get different time sources.
- Various combinator functions now are variadic like `map`
- Add `file/lines` to iterate over lines in a file lazily.
- Reorganize test suite to be sorted by module rather than pseudo-randomly.
- Add `*task-id*`
- Add `env` argument to `fiber/new`.
- Add `JANET_NO_AMALG` flag to Makefile to properly incremental builds
- Optimize bytecode compiler to generate fewer instructions and improve loops.
- Fix bug with `ev/gather` and hung fibers.
- Add `os/isatty`
- Add `has-key?` and `has-value?`
- Make imperative arithmetic macros variadic
- `ev/connect` now yields to the event loop instead of blocking while waiting for an ACK.

## 1.28.0 - 2023-05-13
- Various bug fixes
- Make nested short-fn's behave a bit more predictably (it is still not recommended to nest short-fns).
- Add `os/strftime` for date formatting.
- Fix `ev/select` on threaded channels sometimes live-locking.
- Support the `NO_COLOR` environment variable to turn off VT100 color codes in repl (and in scripts).
  See http://no-color.org/
- Disallow using `(splice x)` in contexts where it doesn't make sense rather than silently coercing to `x`.
  Instead, raise a compiler error.
- Change the names of `:user8` and `:user9` signals to `:interrupt` and `:await`
- Change the names of `:user8` and `:user9` fiber statuses to `:interrupted` and `:suspended`.
- Add `ev/all-tasks` to see all currently suspended fibers.
- Add `keep-syntax` and `keep-syntax!` functions to make writing macros easier.

## 1.27.0 - 2023-03-05
- Change semantics around bracket tuples to no longer be equal to regular tuples.
- Add `index` argument to `ffi/write` for symmetry with `ffi/read`.
- Add `buffer/push-at`
- Add `ffi/pointer-buffer` to convert pointers to buffers the cannot be reallocated. This
  allows easier manipulation of FFI memory, memory mapped files, and buffer memory shared between threads.
- Calling `ev/cancel` on a fiber waiting on `ev/gather` will correctly
  cancel the child fibers.
- Add `(sandbox ...)` function to core for permission based security. Also add `janet_sandbox` to C API.
  The sandbox allows limiting access to the file system, network, ffi, and OS resources at runtime.
- Add `(.locals)` function to debugger to see currently bound local symbols.
- Track symbol -> slot mapping so debugger can get symbolic information. This exposes local bindings
  in `debug/stack` and `disasm`.
- Add `os/compiler` to detect what host compiler was used to compile the interpreter
- Add support for mingw and cygwin builds (mingw support also added in jpm).

## 1.26.0 - 2023-01-07
- Add `ffi/malloc` and `ffi/free`. Useful as tools of last resort.
- Add `ffi/jitfn` to allow calling function pointers generated at runtime from machine code.
  Bring your own assembler, though.
- Channels can now be marshalled. Pending state is not saved, only items in the channel.
- Use the new `.length` function pointer on abstract types for lengths. Adding
  a `length` method will still work as well.
- Support byte views on abstract types with the `.bytes` function pointer.
- Add the `u` format specifier to printf family functions.
- Allow printing 64 integer types in `printf` and `string/format` family functions.
- Allow importing modules from custom directories more easily with the `@` prefix
  to module paths. For example, if there is a dynamic binding :custom-modules that
  is a file system path to a directory of modules, import from that directory with
  `(import @custom-modules/mymod)`.
- Fix error message bug in FFI library.

## 1.25.1 - 2022-10-29
- Add `memcmp` function to core library.
- Fix bug in `os/open` with `:rw` permissions not correct on Linux.
- Support config.mk for more easily configuring the Makefile.

## 1.25.0 - 2022-10-10
- Windows FFI fixes.
- Fix PEG `if-not` combinator with captures in the condition
- Fix bug with `os/date` with nil first argument
- Fix bug with `net/accept` on Linux that could leak file descriptors to subprocesses
- Reduce number of hash collisions from pointer hashing
- Add optional parameter to `marshal` to skip cycle checking code

## 1.24.1 - 2022-08-24
- Fix FFI bug on Linux/Posix
- Improve parse error messages for bad delimiters.
- Add optional `name` parameter to the `short-fn` macro.

## 1.24.0 - 2022-08-14
- Add FFI support to 64-bit windows compiled with MSVC
- Don't process shared object names passed to dlopen.
- Add better support for windows console in the default shell.c for auto-completion and
  other shell-like input features.
- Improve default error message from `assert`.
- Add the `tabseq` macro for simpler table comprehensions.
- Allow setting `(dyn :task-id)` in fibers to improve context in supervisor messages. Prior to
  this change, supervisor messages over threaded channels would be from ambiguous threads/fibers.

## 1.23.0 - 2022-06-20
- Add experimental `ffi/` module for interfacing with dynamic libraries and raw function pointers. Only available
  on 64 bit linux, mac, and bsd systems.
- Allow using `&named` in function prototypes for named arguments. This is a more ergonomic
  variant of `&keys` that isn't as redundant, more self documenting, and allows extension to
  things like default arguments.
- Add `delay` macro for lazy evaluate-and-save thunks.
- Remove pthread.h from janet.h for easier includes.
- Add `debugger` - an easy to use debugger function that just takes a fiber.
- `dofile` will now start a debugger on errors if the environment it is passed has `:debug` set.
- Add `debugger-on-status` function, which can be passed to `run-context` to start a debugger on
  abnormal fiber signals.
- Allow running scripts with the `-d` flag to use the built-in debugger on errors and breakpoints.
- Add mutexes (locks) and reader-writer locks to ev module for thread coordination.
- Add `parse-all` as a generalization of the `parse` function.
- Add `os/cpu-count` to get the number of available processors on a machine

## 1.22.0 - 2022-05-09
- Prohibit negative size argument to `table/new`.
- Add `module/value`.
- Remove `file/popen`. Use `os/spawn` with the `:pipe` options instead.
- Fix bug in peg `thru` and `to` combinators.
- Fix printing issue in `doc` macro.
- Numerous updates to function docstrings
- Add `defdyn` aliases for various dynamic bindings used in core.
- Install `janet.h` symlink to make Janet native libraries and applications
  easier to build without `jpm`.

## 1.21.2 - 2022-04-01
- C functions `janet_dobytes` and `janet_dostring` will now enter the event loop if it is enabled.
- Fix hashing regression - hash of negative 0 must be the same as positive 0 since they are equal.
- The `flycheck` function no longer pollutes the module/cache
- Fix quasiquote bug in compiler
- Disallow use of `cancel` and `resume` on fibers scheduled or created with `ev/go`, as well as the root
  fiber.

## 1.20.0 - 2022-1-27
- Add `:missing-symbol` hook to `compile` that will act as a catch-all macro for undefined symbols.
- Add `:redef` dynamic binding that will allow users to redefine top-level bindings with late binding. This
  is intended for development use.
- Fix a bug with reading from a stream returned by `os/open` on Windows and Linux.
- Add `:ppc64` as a detectable OS type.
- Add `& more` support for destructuring in the match macro.
- Add `& more` support for destructuring in all binding forms (`def`).

## 1.19.2 - 2021-12-06
- Fix bug with missing status lines in some stack traces.
- Update hash function to have better statistical properties.

## 1.19.1 - 2021-12-04
- Add an optional `prefix` parameter to `debug/stacktrace` to allow printing prettier error messages.
- Remove appveyor for CI pipeline
- Fixed a bug that prevented sending threaded abstracts over threaded channels.
- Fix bug in the `map` function with arity at least 3.

## 1.19.0 - 2021-11-27
- Add `math/log-gamma` to replace `math/gamma`, and change `math/gamma` to be the expected gamma function.
- Fix leaking file-descriptors in os/spawn and os/execute.
- Ctrl-C will now raise SIGINT.
- Allow quoted literals in the `match` macro to behave as expected in patterns.
- Fix windows net related bug for TCP servers.
- Allow evaluating ev streams with dofile.
- Fix `ev` related bug with operations on already closed file descriptors.
- Add struct and table agnostic `getproto` function.
- Add a number of functions related to structs.
- Add prototypes to structs. Structs can now inherit from other structs, just like tables.
- Create a struct with a prototype with `struct/with-proto`.
- Deadlocked channels will no longer exit early - instead they will hang, which is more intuitive.

## 1.18.1 - 2021-10-16
- Fix some documentation typos
- Fix - Set pipes passed to subprocess to blocking mode.
- Fix `-r` switch in repl.

## 1.18.0 - 2021-10-10
- Allow `ev/cancel` to work on already scheduled fibers.
- Fix bugs with ev/ module.
- Add optional `base` argument to scan-number
- Add `-i` flag to janet binary to make it easier to run image files from the command line
- Remove `thread/` module.
- Add `(number ...)` pattern to peg for more efficient number parsing using Janet's
  scan-number function without immediate string creation.

## 1.17.2 - 2021-09-18
- Remove include of windows.h from janet.h. This caused issues on certain projects.
- Fix formatting in doc-format to better handle special characters in signatures.
- Fix some marshalling bugs.
- Add optional Makefile target to install jpm as well.
- Supervisor channels in threads will no longer include a wasteful copy of the fiber in every
  message across a thread.
- Allow passing a closure to `ev/thread` as well as a whole fiber.
- Allow passing a closure directly to `ev/go` to spawn fibers on the event loop.

## 1.17.1 - 2021-08-29
- Fix docstring typos
- Add `make install-jpm-git` to make jpm co-install simpler if using the Makefile.
- Fix bugs with starting ev/threads and fiber marshaling.

## 1.17.0 - 2021-08-21
- Add the `-E` flag for one-liners with the `short-fn` syntax for argument passing.
- Add support for threaded abstract types. Threaded abstract types can easily be shared between threads.
- Deprecate the `thread` library. Use threaded channels and ev instead.
- Channels can now be marshalled.
- Add the ability to close channels with `ev/chan-close` (or `:close`).
- Add threaded channels with `ev/thread-chan`.
- Add `JANET_FN` and `JANET_REG` macros to more easily define C functions that export their source mapping information.
- Add `janet_interpreter_interrupt` and `janet_loop1_interrupt` to interrupt the interpreter while running.
- Add `table/clear`
- Add build option to disable the threading library without disabling all threads.
- Remove JPM from the main Janet distribution. Instead, JPM must be installed
  separately like any other package.
- Fix issue with `ev/go` when called with an initial value and supervisor.
- Add the C API functions `janet_vm_save` and `janet_vm_load` to allow
saving and restoring the entire VM state.

## 1.16.1 - 2021-06-09
- Add `maclintf` - a utility for adding linting messages when inside macros.
- Print source code of offending line on compiler warnings and errors.
- Fix some issues with linting and re-add missing `make docs`.
- Allow controlling linting with dynamic bindings `:lint-warn`, `:lint-error`, and `:lint-levels`.
- Add `-w` and `-x` command line flags to the `janet` binary to set linting thresholds.
  linting thresholds are as follows:
    - :none - will never be trigger.
    - :relaxed - will only trigger on `:relaxed` lints.
    - :normal - will trigger on `:relaxed` and `:normal` lints.
    - :strict - will trigger on `:strict`, `:normal`, and `:relaxed` lints. This will catch the most issues
      but can be distracting.

## 1.16.0 - 2021-05-30
- Add color documentation to the `doc` macro - enable/disable with `(dyn :doc-color)`.
- Remove simpler HTML docs from distribution - use website or built-in documentation instead.
- Add compiler warnings and deprecation levels.
- Add `as-macro` to make using macros within quasiquote easier to do hygienically.
- Expose `JANET_OUT_OF_MEMORY` as part of the Janet API.
- Add `native-deps` option to `declare-native` in `jpm`. This lets native libraries link to other
  native libraries when building with jpm.
- Remove the `tarray` module. The functionality of typed arrays will be moved to an external module
  that can be installed via `jpm`.
- Add `from-pairs` to core.
- Add `JPM_OS_WHICH` environment variable to jpm to allow changing auto-detection behavior.
- The flychecker will consider any top-level calls of functions that start with `define-` to
  be safe to execute and execute them. This allows certain patterns (like spork/path) to be
  better processed by the flychecker.

## 1.15.5 - 2021-04-25
- Add `declare-headers` to jpm.
- Fix error using unix pipes on BSDs.
- Support .cc and .cxx extensions in `jpm` for C++ code.
- Change networking code to not create as many HUP errors.
- Add `net/shutdown` to close sockets in one direction without hang ups.
- Update code for printing the debug repl

## 1.15.4 - 2021-03-16
- Increase default nesting depth of pretty printing to `JANET_RECURSION_GUARD`
- Update meson.build
- Add option to automatically add shebang line in installed scripts with `jpm`.
- Add `partition-by` and `group-by` to the core.
- Sort keys in pretty printing output.

## 1.15.3 - 2021-02-28
- Fix a fiber bug that occurred in deeply nested fibers
- Add `unref` combinator to pegs.
- Small docstring changes.

## 1.15.2 - 2021-02-15
- Fix bug in windows version of `os/spawn` and `os/execute` with setting environment variables.
- Fix documentation typos.
- Fix peg integer reading combinators when used with capture tags.

## 1.15.0 - 2021-02-08
- Fix `gtim` and `ltim` bytecode instructions on non-integer values.
- Clean up output of flychecking to be the same as the repl.
- Change behavior of `debug/stacktrace` with a nil error value.
- Add optional argument to `parser/produce`.
- Add `no-core` option to creating standalone binaries to make execution faster.
- Fix bug where a buffer overflow could be confused with an out of memory error.
- Change error output to `file:line:column: message`. Column is in bytes - tabs
  are considered to have width 1 (instead of 8).

## 1.14.2 - 2021-01-23
- Allow `JANET_PROFILE` env variable to load a profile before loading the repl.
- Update `tracev` macro to allow `def` and `var` inside to work as expected.
- Use `(dyn :peg-grammar)` for passing a default grammar to `peg/compile` instead of loading
  `default-peg-grammar` directly from the root environment.
- Add `ev/thread` for combining threading with the event loop.
- Add `ev/do-thread` to make `ev/thread` easier to use.
- Automatically set supervisor channel in `net/accept-loop` and `net/server` correctly.

## 1.14.1 - 2021-01-18
- Add `doc-of` for reverse documentation lookup.
- Add `ev/give-supervsior` to send a message to the supervising channel.
- Add `ev/gather` and `chan` argument to `ev/go`. This new argument allows "supervisor channels"
  for fibers to enable structured concurrency.
- Make `-k` flag work on stdin if no files are given.
- Add `flycheck` function to core.
- Make `backmatch` and `backref` more expressive in pegs.
- Fix buggy `string/split`.
- Add `fiber/last-value` to get the value that was last yielded, errored, or signaled
  by a fiber.
- Remove `:generate` verb from `loop` macros. Instead, use the `:in` verb
  which will now work on fibers as well as other data structures.
- Define `next`, `get`, and `in` for fibers. This lets
  `each`, `map`, and similar iteration macros can now iterate over fibers.
- Remove macro `eachy`, which can be replaced by `each`.
- Add `dflt` argument to find-index.
- Deprecate `file/popen` in favor of `os/spawn`.
- Add `:all` keyword to `ev/read` and `net/read` to make them more like `file/read`. However, we
  do not provide any `:line` option as that requires buffering.
- Change repl behavior to make Ctrl-C raise SIGINT on posix. The old behavior for Ctrl-C,
  to clear the current line buffer, has been moved to Ctrl-Q.
- Importing modules that start with `/` is now the only way to import from project root.
  Before, this would import from / on disk. Previous imports that did not start with `.` or `/`
  are now unambiguously importing from the syspath, instead of checking both the syspath and
  the project root. This is backwards incompatible and dependencies should be updated for this.
- Change hash function for numbers.
- Improve error handling of `dofile`.
- Bug fixes in networking and subprocess code.
- Use markdown formatting in more places for docstrings.

## 1.13.1 - 2020-12-13
- Pretty printing a table with a prototype will look for `:_name` instead of `:name`
  in the prototype table to tag the output.
- `match` macro implementation changed to be tail recursive.
- Adds a :preload loader which allows one to manually put things into `module/cache`.
- Add `buffer/push` function.
- Backtick delimited strings and buffers are now reindented based on the column of the
  opening delimiter. Whitespace in columns to the left of the starting column is ignored unless
  there are non-space/non-newline characters in that region, in which case the old behavior is preserved.
- Argument to `(error)` combinator in PEGs is now optional.
- Add `(line)` and `(column)` combinators to PEGs to capture source line and column.
  This should make error reporting a bit easier.
- Add `merge-module` to core.
- During installation and release, merge janetconf.h into janet.h for easier install.
- Add `upscope` special form.
- `os/execute` and `os/spawn` can take streams for redirecting IO.
- Add `:parser` and `:read` parameters to `run-context`.
- Add `os/open` if ev is enabled.
- Add `os/pipe` if ev is enabled.
- Add `janet_thread_current(void)` to C API
- Add integer parsing forms to pegs. This makes parsing many binary protocols easier.
- Lots of updates to networking code - now can use epoll (or poll) on linux and IOCP on windows.
- Add `ev/` module. This exposes a fiber scheduler, queues, timeouts, and other functionality to users
  for single threaded cooperative scheduling and asynchronous IO.
- Add `net/accept-loop` and `net/listen`. These functions break down `net/server` into it's essential parts
  and are more flexible. They also allow further improvements to these utility functions.
- Various small bug fixes.

## 1.12.2 - 2020-09-20
- Add janet\_try and janet\_restore to C API.
- Fix `os/execute` regression on windows.
- Add :pipe option to `os/spawn`.
- Fix docstring typos.

## 1.12.1 - 2020-09-07
- Make `zero?`, `one?`, `pos?`, and `neg?` polymorphic.
- Add C++ support to jpm and improve C++ interop in janet.h.
- Add `%t` formatter to `printf`, `string/format`, and other formatter functions.
- Expose `janet_cfuns_prefix` in C API.
- Add `os/proc-wait` and `os/proc-kill` for interacting with processes.
- Add `janet_getjfile` to C API.
- Allow redirection of stdin, stdout, and stderr by passing keywords in the env table in `os/spawn` and `os/execute`.
- Add `os/spawn` to get a core/process back instead of an exit code as in `os/execute`.
  When called like this, `os/execute` returns immediately.
- Add `:x` flag to os/execute to raise error when exit code is non-zero.
- Don't run `main` when flychecking.
- Add `:n` flag to `file/open` to raise an error if file cannot be opened.
- Fix import macro to not try and coerce everything to a string.
- Allow passing a second argument to `disasm`.
- Add `cancel`. Resumes a fiber but makes it immediately error at the yield point.
- Allow multi-line paste into built in repl.
- Add `(curenv)`.
- Change `net/read`, `net/chunk`, and `net/write` to raise errors in the case of failures.
- Add `janet_continue_signal` to C API. This indirectly enables C functions that yield to the event loop
  to raise errors or other signals.
- Update meson build script to fix bug on Debian's version of meson
- Add `xprint`, `xprin`, `xprintf`, and `xprinf`.
- `net/write` now raises an error message if write fails.
- Fix issue with SIGPIPE on macOS and BSDs.

## 1.11.3 - 2020-08-03
- Add `JANET_HASHSEED` environment variable when `JANET_PRF` is enabled.
- Expose `janet_cryptorand` in C API.
- Properly initialize PRF in default janet program
- Add `index-of` to core library.
- Add `-fPIC` back to core CFLAGS (non-optional when compiling default client with Makefile)
- Fix defaults on Windows for ARM
- Fix defaults on NetBSD.

## 1.11.1 - 2020-07-25
- Fix jpm and git with multiple git installs on Windows
- Fix importing a .so file in the current directory
- Allow passing byte sequence types directly to typed-array constructors.
- Fix bug sending files between threads.
- Disable PRF by default.
- Update the soname.

## 1.11.0 - 2020-07-18
- Add `forever` macro.
- Add `any?` predicate to core.
- Add `jpm list-pkgs` subcommand to see which package aliases are in the listing.
- Add `jpm list-installed` subcommand to see which packages are installed.
- Add `math/int-min`, `math/int-max`, `math/int32-min`, and `math/int32-max` for getting integer limits.
- The gc interval is now autotuned, to prevent very bad gc behavior.
- Improvements to the bytecode compiler, Janet will now generate more efficient bytecode.
- Add `peg/find`, `peg/find-all`, `peg/replace`, and `peg/replace-all`
- Add `math/nan`
- Add `forv` macro
- Add `symbol/slice`
- Add `keyword/slice`
- Allow cross compilation with Makefile.
- Change `compare-primitive` to `cmp` and make it more efficient.
- Add `reverse!` for reversing an array or buffer in place.
- `janet_dobytes` and `janet_dostring` return parse errors in \*out
- Add `repeat` macro for iterating something n times.
- Add `eachy` (each yield) macro for iterating a fiber.
- Fix `:generate` verb in loop macro to accept non symbols as bindings.
- Add `:h`, `:h+`, and `:h*` in `default-peg-grammar` for hexadecimal digits.
- Fix `%j` formatter to print numbers precisely (using the `%.17g` format string to printf).

## 1.10.1 - 2020-06-18
- Expose `janet_table_clear` in API.
- Respect `JANET_NO_PROCESSES` define when building
- Fix `jpm` rules having multiple copies of the same dependency.
- Fix `jpm` install in some cases.
- Add `array/trim` and `buffer/trim` to shrink the backing capacity of these types
  to their current length.

## 1.10.0 - 2020-06-14
- Hardcode default jpm paths on install so env variables are needed in fewer cases.
- Add `:no-compile` to `create-executable` option for jpm.
- Fix bug with the `trace` function.
- Add `:h`, `:a`, and `:c` flags to `thread/new` for creating new kinds of threads.
  By default, threads will now consume much less memory per thread, but sending data between
  threads may cost more.
- Fix flychecking when using the `use` macro.
- CTRL-C no longer exits the repl, and instead cancels the current form.
- Various small bug fixes
- New MSI installer instead of NSIS based installer.
- Make `os/realpath` work on windows.
- Add polymorphic `compare` functions for comparing numbers.
- Add `to` and `thru` peg combinators.
- Add `JANET_GIT` environment variable to jpm to use a specific git binary (useful mainly on windows).
- `asm` and `disasm` functions now use keywords instead of macros for keys. Also
  some slight changes to the way constants are encoded (remove wrapping `quote` in some cases).
- Expose current macro form inside macros as (dyn :macro-form)
- Add `tracev` macro.
- Fix compiler bug that emitted incorrect code in some cases for while loops that create closures.
- Add `:fresh` option to `(import ...)` to overwrite the module cache.
- `(range x y 0)` will return an empty array instead of hanging forever.
- Rename `jpm repl` to `jpm debug-repl`.

## 1.9.1 - 2020-05-12
- Add :prefix option to declare-source
- Re-enable minimal builds with the debugger.
- Add several flags for configuring Janet on different platforms.
- Fix broken meson build from 1.9.0 and add meson to CI.
- Fix compilation issue when nanboxing is disabled.

## 1.9.0 - 2020-05-10
- Add `:ldflags` option to many jpm declare functions.
- Add `errorf` to core.
- Add `lenprefix` combinator to PEGs.
- Add `%M`, `%m`, `%N`, and `%n` formatters to formatting functions. These are the
  same as `%Q`, `%q`, `%P`, and `%p`, but will not truncate long values.
- Add `fiber/root`.
- Add beta `net/` module to core for socket based networking.
- Add the `parse` function to parse strings of source code more conveniently.
- Add `jpm rule-tree` subcommand.
- Add `--offline` flag to jpm to force use of the cache.
- Allow sending pointers and C functions across threads via `thread/send`.
- Fix bug in `getline`.
- Add `sh-rule` and `sh-phony` to jpm's dialect of Janet.
- Change C api's `janet_formatb` -> `janet_formatbv`, and add new function `janet_formatb` to C api.
- Add `edefer` macro to core.
- A struct/table literal/constructor with duplicate keys will use the last value given.
  Previously, this was inconsistent between tables and structs, literals and constructor functions.
- Add debugger to core. The debugger functions are only available
  in a debug repl, and are prefixed by a `.`.
- Add `sort-by` and `sorted-by` to core.
- Support UTF-8 escapes in strings via `\uXXXX` or `\UXXXXXX`.
- Add `math/erf`
- Add `math/erfc`
- Add `math/log1p`
- Add `math/next`
- Add os/umask
- Add os/perm-int
- Add os/perm-string
- Add :int-permissions option for os/stat.
- Add `jpm repl` subcommand, as well as `post-deps` macro in project.janet files.
- Various bug fixes.

## 1.8.1 - 2020-03-31
- Fix bugs for big endian systems
- Fix 1.8.0 regression on BSDs

## 1.8.0 - 2020-03-29
- Add `reduce2`, `accumulate`, and `accumulate2`.
- Add lockfiles to `jpm` via `jpm make-lockfile` and `jpm load-lockfile`.
- Add `os/realpath` (Not supported on windows).
- Add `os/chmod`.
- Add `chr` macro.
- Allow `_` in the `match` macro to match anything without creating a binding
  or doing unification. Also change behavior of matching nil.
- Add `:range-to` and `:down-to` verbs in the `loop` macro.
- Fix `and` and `or` macros returning nil instead of false in some cases.
- Allow matching successfully against nil values in the `match` macro.
- Improve `janet_formatc` and `janet_panicf` formatters to be more like `string/format`.
  This makes it easier to make nice error messages from C.
- Add `signal`
- Add `fiber/can-resume?`
- Allow fiber functions to accept arguments that are passed in via `resume`.
- Make flychecking slightly less strict but more useful
- Correct arity for `next`
- Correct arity for `marshal`
- Add `flush` and `eflush`
- Add `prompt` and `return` on top of signal for user friendly delimited continuations.
- Fix bug in buffer/blit when using the offset-src argument.
- Fix segfault with malformed pegs.

## 1.7.0 - 2020-02-01
- Remove `file/fileno` and `file/fdopen`.
- Remove `==`, `not==`, `order<`, `order>`, `order<=`, and `order>=`. Instead, use the normal
  comparison and equality functions.
- Let abstract types define a hash function and comparison/equality semantics. This lets
  abstract types much better represent value types. This adds more fields to abstract types, which
  will generate warnings when compiled against other versions.
- Remove Emscripten build. Instead, use the amalgamated source code with a custom toolchain.
- Update documentation.
- Add `var-`
- Add `module/add-paths`
- Add `file/temp`
- Add `mod` function to core.
- Small bug fixes
- Allow signaling from C functions (yielding) via janet\_signalv. This
  makes it easy to write C functions that work with event loops, such as
  in libuv or embedded in a game.
- Add '%j' formatting option to the format family of functions.
- Add `defer`
- Add `assert`
- Add `when-with`
- Add `if-with`
- Add completion to the default repl based on currently defined bindings. Also generally improve
  the repl keybindings.
- Add `eachk`
- Add `eachp`
- Improve functionality of the `next` function. `next` now works on many different
  types, not just tables and structs. This allows for more generic data processing.
- Fix thread module issue where sometimes decoding a message failed.
- Fix segfault regression when macros are called with bad arity.

## 1.6.0 - 2019-12-22
- Add `thread/` module to the core.
- Allow seeding RNGs with any sequence of bytes. This provides
  a wider key space for the RNG. Exposed in C as `janet_rng_longseed`.
- Fix issue in `resume` and similar functions that could cause breakpoints to be skipped.
- Add a number of new math functions.
- Improve debugger experience and capabilities. See examples/debugger.janet
  for what an interactive debugger could look like.
- Add `debug/step` (janet\_step in the C API) for single stepping Janet bytecode.
- The built in repl now can enter the debugger on any signal (errors, yields,
  user signals, and debug signals). To enable this, type (setdyn :debug true)
  in the repl environment.
- When exiting the debugger, the fiber being debugged is resumed with the exit value
  of the debug session (the value returned by `(quit return-value)`, or nil if user typed Ctrl-D).
- `(quit)` can take an optional argument that is the return value. If a module
  contains `(quit some-value)`, the value of that module returned to `(require "somemod")`
  is the return value. This lets module writers completely customize a module without writing
  a loader.
- Add nested quasiquotation.
- Add `os/cryptorand`
- Add `prinf` and `eprinf` to be have like `printf` and `eprintf`. The latter two functions
  now including a trailing newline, like the other print functions.
- Add nan?
- Add `janet_in` to C API.
- Add `truthy?`
- Add `os/environ`
- Add `buffer/fill` and `array/fill`
- Add `array/new-filled`
- Use `(doc)` with no arguments to see available bindings and dynamic bindings.
- `jpm` will use `CC` and `AR` environment variables when compiling programs.
- Add `comptime` macro for compile time evaluation.
- Run `main` functions in scripts if they exist, just like jpm standalone binaries.
- Add `protect` macro.
- Add `root-env` to get the root environment table.
- Change marshalling protocol with regard to abstract types.
- Add `show-paths` to `jpm`.
- Add several default patterns, like `:d` and `:s+`, to PEGs.
- Update `jpm` path settings to make using `jpm` easier on non-global module trees.
- Numerous small bug fixes and usability improvements.

### 1.5.1 - 2019-11-16
- Fix bug when printing buffer to self in some edge cases.
- Fix bug with `jpm` on windows.
- Fix `update` return value.

## 1.5.0 - 2019-11-10
- `os/date` now defaults to UTC.
- Add `--test` flag to jpm to test libraries on installation.
- Add `math/rng`, `math/rng-int`, and `math/rng-uniform`.
- Add `in` function to index in a stricter manner. Conversely, `get` will
  now not throw errors on bad keys.
- Indexed types and byte sequences will now error when indexed out of range or
  with bad keys.
- Add rng functions to Janet. This also replaces the RNG behind `math/random`
  and `math/seedrandom` with a consistent, platform independent RNG.
- Add `with-vars` macro.
- Add the `quickbin` command to jpm.
- Create shell.c when making the amalgamated source. This can be compiled with
  janet.c to make the janet interpreter.
- Add `cli-main` function to the core, which invokes Janet's CLI interface.
  This basically moves what was init.janet into boot.janet.
- Improve flychecking, and fix flychecking bugs introduced in 1.4.0.
- Add `prin`, `eprint`, `eprintf` and `eprin` functions. The
  functions prefix with e print to `(dyn :err stderr)`
- Print family of functions can now also print to buffers
  (before, they could only print to files.) Output can also
  be completely disabled with `(setdyn :out false)`.
- `printf` is now a c function for optimizations in the case
  of printing to buffers.

## 1.4.0 - 2019-10-14
- Add `quit` function to exit from a repl, but not always exit the entire
  application.
- Add `update-pkgs` to jpm.
- Integrate jpm with https://github.com/janet-lang/pkgs.git. jpm can now
  install packages based on their short names in the package listing, which
  can be customized via an env variable.
- Add `varfn` macro
- Add compile time arity checking when function in function call is known.
- Added `slice` to the core library.
- The `*/slice` family of functions now can take nil as start or end to get
  the same behavior as the defaults (0 and -1) for those parameters.
- `string/` functions that take a pattern to search for will throw an error
  when receiving the empty string.
- Replace (start:end) style stacktrace source position information with
  line, column. This should be more readable for humans. Also, range information
  can be recovered by re-parsing source.

## 1.3.1 - 2019-09-21
- Fix some linking issues when creating executables with native dependencies.
- jpm now runs each test script in a new interpreter.
- Fix an issue that prevent some valid programs from compiling.
- Add `mean` to core.
- Abstract types that implement the `:+`, `:-`, `:*`, `:/`, `:>`, `:==`, `:<`,
  `:<=`, and `:>=` methods will work with the corresponding built-in
  arithmetic functions. This means built-in integer types can now be used as
  normal number values in many contexts.
- Allow (length x) on typed arrays an other abstract types that implement
  the :length method.

## 1.3.0 - 2019-09-05
- Add `get-in`, `put-in`, `update-in`, and `freeze` to core.
- Add `jpm run rule` and `jpm rules` to jpm to improve utility and discoverability of jpm.
- Remove `cook` module and move `path` module to https://github.com/janet-lang/path.git.
  The functionality in `cook` is now bundled directly in the `jpm` script.
- Add `buffer/format` and `string/format` format flags `Q` and `q` to print colored and
  non-colored single-line values, similar to `P` and `p`.
- Change default repl to print long sequences on one line and color stacktraces if color is enabled.
- Add `backmatch` pattern for PEGs.
- jpm detects if not in a Developer Command prompt on windows for a better error message.
- jpm install git submodules in dependencies
- Change default fiber stack limit to the maximum value of a 32 bit signed integer.
- Some bug fixes with `jpm`
- Fix bugs with pegs.
- Add `os/arch` to get ISA that janet was compiled for
- Add color to stacktraces via `(dyn :err-color)`

## 1.2.0 - 2019-08-08
- Add `take` and `drop` functions that are easier to use compared to the
  existing slice functions.
- Add optional default value to `get`.
- Add function literal short-hand via `|` reader macro, which maps to the
  `short-fn` macro.
- Add `int?` and `nat?` functions to the core.
- Add `(dyn :executable)` at top level to get what used to be
  `(process/args 0)`.
- Add `:linux` to platforms returned by `(os/which)`.
- Update jpm to build standalone executables. Use `declare-executable` for this.
- Add `use` macro.
- Remove `process/args` in favor of `(dyn :args)`.
- Fix bug with Nanbox implementation allowing users to created
  custom values of any type with typed array and marshal modules, which
  was unsafe.
- Add `janet_wrap_number_safe` to API, for converting numbers to Janets
  where the number could be any 64 bit, user provided bit pattern. Certain
  NaN values (which a machine will never generate as a result of a floating
  point operation) are guarded against and converted to a default NaN value.

## 1.1.0 - 2019-07-08
- Change semantics of `-l` flag to be import rather than dofile.
- Fix compiler regression in top level defs with destructuring.
- Add `table/clone`.
- Improve `jpm` tool with git and dependency capabilities, as well as better
  module uninstalls.

## 1.0.0 - 2019-07-01
- Add `with` macro for resource handling.
- Add `propagate` function so we can "rethrow" signals after they are
  intercepted. This makes signals even more flexible.
- Add `JANET_NO_DOCSTRINGS` and `JANET_NO_SOURCEMAPS` defines in janetconf.h
  for shrinking binary size.
  This seems to save about 50kB in most builds, so it's not usually worth it.
- Update module system to allow relative imports. The `:cur:` pattern
  in `module/expand-path` will expand to the directory part of the current file, or
  whatever the value of `(dyn :current-file)` is. The `:dir:` pattern gets
  the directory part of the input path name.
- Remove `:native:` pattern in `module/paths`.
- Add `module/expand-path`
- Remove `module/*syspath*` and `module/*headerpath*` in favor of dynamic
  bindings `:syspath` and `:headerpath`.
- Compiled PEGs can now be marshaled and unmarshaled.
- Change signature to `parser/state`
- Add `:until` verb to loop.
- Add `:p` flag to `fiber/new`.
- Add `file/{fdopen,fileno}` functions.
- Add `parser/clone` function.
- Add optional argument to `parser/where` to set parser byte index.
- Add optional `env` argument to `all-bindings` and `all-dynamics`.
- Add scratch memory C API functions for auto-released memory on next gc.
  Scratch memory differs from normal GCed memory as it can also be freed normally
  for better performance.
- Add API compatibility checking for modules. This will let native modules not load
  when the host program is not of a compatible version or configuration.
- Change signature of `os/execute` to be much more flexible.

## 0.6.0 - 2019-05-29
- `file/close` returns exit code when closing file opened with `file/popen`.
- Add `os/rename`
- Update windows installer to include tools like `jpm`.
- Add `jpm` tool for building and managing projects.
- Change interface to `cook` tool.
- Add optional filters to `module/paths` to further refine import methods.
- Add keyword arguments via `&keys` in parameter list.
- Add `-k` flag for flychecking source.
- Change signature to `compile` function.
- Add `module/loaders` for custom loading functions.
- Add external unification to `match` macro.
- Add static library to main build.
- Add `janet/*headerpath*` and change location of installed headers.
- Let `partition` take strings.
- Haiku OS support
- Add `string/trim`, `string/trimr`, and `string/triml`.
- Add `dofile` function.
- Numbers require at least 1 significant digit.
- `file/read` will return nil on end of file.
- Fix various bugs.

## 0.5.0 - 2019-05-09
- Fix some bugs with buffers.
- Add `trace` and `untrace` to the core library.
- Add `string/has-prefix?` and `string/has-suffix?` to string module.
- Add simple debugger to repl that activates on errors or debug signal
- Remove `*env*` and `*doc-width*`.
- Add `fiber/getenv`, `fiber/setenv`, and `dyn`, and `setdyn`.
- Add support for dynamic bindings (via the `dyn` and `setdyn` functions).
- Change signatures of some functions like `eval` which no longer takes
  an optional environment.
- Add printf function
- Make `pp` configurable with dynamic binding `:pretty-format`.
- Remove the `meta` function.
- Add `with-dyns` for blocks with dynamic bindings assigned.
- Allow leading and trailing newlines in backtick-delimited string (long strings).
  These newlines will not be included in the actual string value.

## 0.4.1 - 2019-04-14
- Squash some bugs
- Peg patterns can now make captures in any position in a grammar.
- Add color to repl output
- Add array/remove function
- Add meson build support
- Add int module for int types
- Add meson build option
- Add (break) special form and improve loop macro
- Allow abstract types to specify custom tostring method
- Extend C API for marshalling abstract types and other values
- Add functions to `os` module.

## 0.4.0 - 2019-03-08
- Fix a number of smaller bugs
- Added :export option to import and require
- Added typed arrays
- Remove `callable?`.
- Remove `tuple/append` and `tuple/prepend`, which may have seemed like `O(1)`
  operations. Instead, use the `splice` special to extend tuples.
- Add `-m` flag to main client to allow specifying where to load
  system modules from.
- Add `-c` flag to main client to allow compiling Janet modules to images.
- Add `string/format` and `buffer/format`.
- Remove `string/pretty` and `string/number`.
- `make-image` function creates pre compiled images for janet. These images
  link to the core library. They can be loaded via require or manually via
  `load-image`.
- Add bracketed tuples as tuple constructor.
- Add partition function to core library.
- Pre-compile core library into an image for faster startup.
- Add methods to parser values that mirror the api.
- Add janet\_getmethod to CAPI for easier use of method like syntax.
- Add get/set to abstract types to allow them to behave more
  like objects with methods.
- Add parser/insert to modify parser state programmatically
- Add debug/stacktrace for easy, pretty stacktraces
- Remove the status-pp function
- Update API to run-context to be much more sane
- Add :lflags option to cook/make-native
- Disallow NaNs as table or struct keys
- Update module resolution paths and format

## 0.3.0 - 2019-01-26
- Add amalgamated build to janet for easier embedding.
- Add os/date function
- Add slurp and spit to core library.
- Added this changelog.
- Added peg module (Parsing Expression Grammars)
- Move hand written documentation into website repository.
