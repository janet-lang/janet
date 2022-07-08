# Copyright (c) 2021 Calvin Rose & contributors
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

# Subprocess

(def janet (dyn :executable))

(repeat 10

  (let [p (os/spawn [janet "-e" `(print "hello")`] :p {:out :pipe})]
    (os/proc-wait p)
    (def x (:read (p :out) :all))
    (assert (deep= "hello" (string/trim x)) "capture stdout from os/spawn pre close."))

  (let [p (os/spawn [janet "-e" `(print "hello")`] :p {:out :pipe})]
    (def x (:read (p :out) 1024))
    (os/proc-wait p)
    (assert (deep= "hello" (string/trim x)) "capture stdout from os/spawn post close."))

  (let [p (os/spawn [janet "-e" `(file/read stdin :line)`] :px {:in :pipe})]
    (:write (p :in) "hello!\n")
    (assert-no-error "pipe stdin to process" (os/proc-wait p))))

(let [p (os/spawn [janet "-e" `(print (file/read stdin :line))`] :px {:in :pipe :out :pipe})]
  (:write (p :in) "hello!\n")
  (def x (:read (p :out) 1024))
  (assert-no-error "pipe stdin to process 2" (os/proc-wait p))
  (assert (= "hello!" (string/trim x)) "round trip pipeline in process"))

(let [p (os/spawn [janet "-e" `(do (ev/sleep 30) (os/exit 24)`] :p)]
  (os/proc-kill p)
  (def retval (os/proc-wait p))
  (assert (not= retval 24) "Process was *not* terminated by parent"))

# Parallel subprocesses

(defn calc-1
  "Run subprocess, read from stdout, then wait on subprocess."
  [code]
  (let [p (os/spawn [janet "-e" (string `(printf "%j" ` code `)`)] :px {:out :pipe})]
    (os/proc-wait p)
    (def output (:read (p :out) :all))
    (parse output)))

(assert
  (deep=
    (ev/gather
      (calc-1 "(+ 1 2 3 4)")
      (calc-1 "(+ 5 6 7 8)")
      (calc-1 "(+ 9 10 11 12)"))
    @[10 26 42]) "parallel subprocesses 1")

(defn calc-2
  "Run subprocess, wait on subprocess, then read from stdout. Read only up to 10 bytes instead of :all"
  [code]
  (let [p (os/spawn [janet "-e" (string `(printf "%j" ` code `)`)] :px {:out :pipe})]
    (def output (:read (p :out) 10))
    (os/proc-wait p)
    (parse output)))

(assert
  (deep=
    (ev/gather
      (calc-2 "(+ 1 2 3 4)")
      (calc-2 "(+ 5 6 7 8)")
      (calc-2 "(+ 9 10 11 12)"))
    @[10 26 42]) "parallel subprocesses 2")

# File piping

(assert-no-error "file writing 1"
  (with [f (file/temp)]
    (os/execute [janet "-e" `(repeat 20 (print :hello))`] :p {:out f})))

(assert-no-error "file writing 2"
  (with [f (file/open "unique.txt" :w)]
    (os/execute [janet "-e" `(repeat 20 (print :hello))`] :p {:out f})
    (file/flush f)))

# Issue #593
(assert-no-error "file writing 3"
  (def outfile (file/open "unique.txt" :w))
  (os/execute [janet "-e" "(pp (seq [i :range (1 10)] i))"] :p {:out outfile})
  (file/flush outfile)
  (file/close outfile)
  (os/rm "unique.txt"))

# Ensure that the stream created by os/open works

(assert-no-error "File writing 4.1"
   (def outstream (os/open "unique.txt" :wct))
   (defer (:close outstream)
     (:write outstream "123\n")
     (:write outstream "456\n"))
   # Cast to string to enable comparison
   (assert (= "123\n456\n" (string (slurp "unique.txt"))) "File writing 4.2")
   (os/rm "unique.txt"))

# Test that the stream created by os/open can be read from
(comment
  (assert-no-error "File reading 1.1"
    (def outstream (os/open "unique.txt" :wct))
    (defer (:close outstream)
      (:write outstream "123\n")
      (:write outstream "456\n"))

    (def outstream (os/open "unique.txt" :r))
    (defer (:close outstream)
      (assert (= "123\n456\n" (string (:read outstream :all))) "File reading 1.2"))
    (os/rm "unique.txt")))

  # ev/gather

(assert (deep= @[1 2 3] (ev/gather 1 2 3)) "ev/gather 1")
(assert (deep= @[] (ev/gather)) "ev/gather 2")
(assert-error "ev/gather 3" (ev/gather 1 2 (error 3)))

# Net testing

(repeat 10

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

# Test on both server and client
(defn names-handler
  [stream]
  (defer (:close stream)
    # prevent immediate close
    (ev/read stream 1)
    (def [host port] (net/localname stream))
    (assert (= host "127.0.0.1") "localname host server")
    (assert (= port 8000) "localname port server")))

# Test localname and peername
(repeat 10
  (with [s (net/server "127.0.0.1" "8000" names-handler)]
    (repeat 10
      (with [conn (net/connect "127.0.0.1" "8000")]
        (def [host port] (net/peername conn))
        (assert (= host "127.0.0.1") "peername host client ")
        (assert (= port 8000) "peername port client")
        # let server close
        (ev/write conn " "))))
  (gccollect))

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

(assert (os/execute [janet "-e" `(+ 1 2 3)`] :xp) "os/execute self")

# Test some channel

(def c1 (ev/chan))
(def c2 (ev/chan))
(def arr @[])
(ev/spawn
  (while (def x (ev/take c1))
    (array/push arr x))
  (ev/chan-close c2))
(for i 0 1000
  (ev/give c1 i))
(ev/chan-close c1)
(ev/take c2)
(assert (= (slice arr) (slice (range 1000))) "ev/chan-close 1")

(def c1 (ev/chan))
(def c2 (ev/chan))
(def arr @[])
(ev/spawn
  (while (def x (ev/take c1))
    (array/push arr x))
  (ev/sleep 0.1)
  (ev/chan-close c2))
(for i 0 100
  (ev/give c1 i))
(ev/chan-close c1)
(ev/select c2)
(assert (= (slice arr) (slice (range 100))) "ev/chan-close 2")

(def c1 (ev/chan))
(def c2 (ev/chan))
(def arr @[])
(ev/spawn
  (while (def x (ev/take c1))
    (array/push arr x))
  (ev/chan-close c2))
(for i 0 100
  (ev/give c1 i))
(ev/chan-close c1)
(ev/rselect c2)
(assert (= (slice arr) (slice (range 100))) "ev/chan-close 3")

# threaded channels

(def ch (ev/thread-chan 2))
(def att (ev/thread-chan 109))
(assert att "`att` was nil after creation")
(ev/give ch att)
(ev/do-thread
  (assert (ev/take ch) "channel packing bug for threaded abstracts on threaded channels."))

(end-suite)
