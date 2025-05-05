# Copyright (c) 2025 Calvin Rose
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

(import ./helper :prefix "" :exit true)
(start-suite)

(use ../examples/sysir/frontend)
(assert true) # smoke test

(def janet (dyn *executable*))
(def run (filter next (string/split " " (os/getenv "SUBRUN" ""))))

(defn do-expect-directory
  "Iterate a directory, evaluating all scripts in the directory. Assert that the captured output of the script
  is as expected according to a matching .expect file."
  [dir]
  (each path (sorted (os/dir dir))
    (when (string/has-suffix? ".janet" path)
      (def fullpath (string dir "/" path))
      (def proc (os/spawn [;run janet fullpath] :p {:out :pipe :err :out}))
      (def buff @"")
      (var ret-code nil)
      (ev/gather
        (while (ev/read (proc :out) 4096 buff))
        (set ret-code (os/proc-wait proc)))
      (def expect-file (string dir "/" path ".expect"))
      (def expected-out (slurp expect-file))
      (assert (= (string/trim expected-out) (string/trim buff))
              (string "\nfile: " fullpath "\nexpected:\n======\n" expected-out "\n======\ngot:\n======\n" buff "\n======\n")))))

(do-expect-directory "test/sysir")

(end-suite)
