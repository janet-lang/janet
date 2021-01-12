# Copyright (c) 2020 Calvin Rose & contributors
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
(start-suite 9)

(repeat 10
  # Subprocess
  (let [p (os/spawn [(dyn :executable) "-e" `(print "hello")`] :p {:out :pipe})]
    (os/proc-wait p)
    (def x (:read (p :out) 1024))
    (assert (deep= "hello" (string/trim x)) "capture stdout from os/spawn pre close."))

  (let [p (os/spawn [(dyn :executable) "-e" `(print "hello")`] :p {:out :pipe})]
    (def x (:read (p :out) 1024))
    (os/proc-wait p)
    (assert (deep= "hello" (string/trim x)) "capture stdout from os/spawn post close."))

  (let [p (os/spawn [(dyn :executable) "-e" `(file/read stdin :line)`] :px {:in :pipe})]
    (:write (p :in) "hello!")
    (assert-no-error "pipe stdin to process" (os/proc-wait p))))

# Net testing

(repeat 30

  (defn handler
    "Simple handler for connections."
    [stream]
    (defer (:close stream)
      (def id (gensym))
      (def b @"")
      (net/read stream 1024 b)
      (net/write stream b)
      (buffer/clear b)))

  (def s (net/server "127.0.0.1" "8000" handler))
  (assert s "made server 1")

  (defn test-echo [msg]
    (with [conn (net/connect "127.0.0.1" "8000")]
      (net/write conn msg)
      (def res (net/read conn 1024))
      (assert (= (string res) msg) (string "echo " msg))))

  (test-echo "hello")
  (test-echo "world")
  (test-echo (string/repeat "abcd" 200))

  (:close s))

# Create pipe

(var pipe-counter 0)
(def chan (ev/chan 10))
(let [[reader writer] (os/pipe)]
  (ev/spawn
    (while (ev/read reader 3)
      (++ pipe-counter))
    (assert (= 20 pipe-counter) "ev/pipe 1")
    (ev/give chan 1))

  (for i 0 10
    (ev/write writer "xxx---"))

  (ev/close writer)
  (ev/take chan))

(var result nil)
(var fiber nil)
(set fiber
  (ev/spawn 
    (set result (protect (ev/sleep 10)))
    (assert (= result '(false "boop")) "ev/cancel 1")))
(ev/sleep 0)
(ev/cancel fiber "boop")

(assert-error "bad arity to ev/call" (ev/call inc 1 2 3))

(assert (os/execute [(dyn :executable) "-e" `(+ 1 2 3)`] :xp) "os/execute self")

(end-suite)
