# Copyright (c) 2023 Calvin Rose & contributors
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

(def test-port (os/getenv "JANET_TEST_PORT" "8761"))
(def test-host (os/getenv "JANET_TEST_HOST" "127.0.0.1"))

# Subprocess
# 5e1a8c86f
(def janet (dyn *executable*))

# Subprocess should inherit the "RUN" parameter for fancy testing
(def run (filter next (string/split " " (os/getenv "SUBRUN" ""))))

(repeat 10

  (let [p (os/spawn [;run janet "-e" `(print "hello")`] :p {:out :pipe})]
    (os/proc-wait p)
    (def x (:read (p :out) :all))
    (assert (deep= "hello" (string/trim x))
            "capture stdout from os/spawn pre close."))

  (let [p (os/spawn [;run janet "-e" `(print "hello")`] :p {:out :pipe})]
    (def x (:read (p :out) 1024))
    (os/proc-wait p)
    (assert (deep= "hello" (string/trim x))
            "capture stdout from os/spawn post close."))

  (let [p (os/spawn [;run janet "-e" `(file/read stdin :line)`] :px
                    {:in :pipe})]
    (:write (p :in) "hello!\n")
    (assert-no-error "pipe stdin to process" (os/proc-wait p))))

(let [p (os/spawn [;run janet "-e" `(print (file/read stdin :line))`] :px
                  {:in :pipe :out :pipe})]
  (:write (p :in) "hello!\n")
  (def x (:read (p :out) 1024))
  (assert-no-error "pipe stdin to process 2" (os/proc-wait p))
  (assert (= "hello!" (string/trim x)) "round trip pipeline in process"))

(let [p (os/spawn [;run janet "-e" `(do (ev/sleep 30) (os/exit 24)`] :p)]
  (os/proc-kill p)
  (def retval (os/proc-wait p))
  (assert (not= retval 24) "Process was *not* terminated by parent"))

(let [p (os/spawn [;run janet "-e" `(do (ev/sleep 30) (os/exit 24)`] :p)]
  (os/proc-kill p false :term)
  (def retval (os/proc-wait p))
  (assert (not= retval 24) "Process was *not* terminated by parent"))

# Parallel subprocesses
# 5e1a8c86f
(defn calc-1
  "Run subprocess, read from stdout, then wait on subprocess."
  [code]
  (let [p (os/spawn [;run janet "-e" (string `(printf "%j" ` code `)`)] :px
                    {:out :pipe})]
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
  ``
  Run subprocess, wait on subprocess, then read from stdout. Read only up
  to 10 bytes instead of :all
  ``
  [code]
  (let [p (os/spawn [;run janet "-e" (string `(printf "%j" ` code `)`)] :px
                    {:out :pipe})]
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
# a1cc5ca04
(assert-no-error "file writing 1"
  (with [f (file/temp)]
    (os/execute [;run janet "-e" `(repeat 20 (print :hello))`] :p {:out f})))

(assert-no-error "file writing 2"
  (with [f (file/open "unique.txt" :w)]
    (os/execute [;run janet "-e" `(repeat 20 (print :hello))`] :p {:out f})
    (file/flush f)))

# Issue #593
# a1cc5ca04
(assert-no-error "file writing 3"
  (def outfile (file/open "unique.txt" :w))
  (os/execute [;run janet "-e" "(pp (seq [i :range (1 10)] i))"] :p
              {:out outfile})
  (file/flush outfile)
  (file/close outfile)
  (os/rm "unique.txt"))

# each-line iterator
# 70f13f1
(assert-no-error "file/lines iterator"
   (def outstream (os/open "unique.txt" :wct))
   (def buf1 "123\n456\n")
   (defer (:close outstream)
     (:write outstream buf1))
   (var buf2 "")
   (with [f (file/open "unique.txt" :r)]
     (each line (file/lines f)
        (set buf2 (string buf2 line))))
   (assert (= buf1 buf2) "file/lines iterator")
   (os/rm "unique.txt"))

# Ensure that the stream created by os/open works
# e8a86013d
(assert-no-error "File writing 4.1"
   (def outstream (os/open "unique.txt" :wct))
   (defer (:close outstream)
     (:write outstream "123\n")
     (:write outstream "456\n"))
   # Cast to string to enable comparison
   (assert (= "123\n456\n" (string (slurp "unique.txt")))
           "File writing 4.2")
   (os/rm "unique.txt"))

# Test that the stream created by os/open can be read from
# 8d8a6534e
(comment
  (assert-no-error "File reading 1.1"
    (def outstream (os/open "unique.txt" :wct))
    (defer (:close outstream)
      (:write outstream "123\n")
      (:write outstream "456\n"))

    (def outstream (os/open "unique.txt" :r))
    (defer (:close outstream)
      (assert (= "123\n456\n" (string (:read outstream :all)))
              "File reading 1.2"))
    (os/rm "unique.txt")))

# ev/gather
# 4f2d1cdc0
(assert (deep= @[1 2 3] (ev/gather 1 2 3)) "ev/gather 1")
(assert (deep= @[] (ev/gather)) "ev/gather 2")
(assert-error "ev/gather 3" (ev/gather 1 2 (error 3)))

(var cancel-counter 0)
(assert-error "ev/gather 4.1" (ev/gather
                               (defer (++ cancel-counter) (ev/take (ev/chan)))
                               (defer (++ cancel-counter) (ev/take (ev/chan)))
                               (error :oops)))
(assert (= cancel-counter 2) "ev/gather 4.2")

# Net testing
# 2904c19ed
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

  (def s (net/server test-host test-port handler))
  (assert s "made server 1")

  (defn test-echo [msg]
    (with [conn (net/connect test-host test-port)]
      (net/write conn msg)
      (def res (net/read conn 1024))
      (assert (= (string res) msg) (string "echo " msg))))

  (test-echo "hello")
  (test-echo "world")
  (test-echo (string/repeat "abcd" 200))

  (:close s)
  (gccollect))

# Test on both server and client
# 504411e
(defn names-handler
  [stream]
  (defer (:close stream)
    # prevent immediate close
    (ev/read stream 1)
    (def [host port] (net/localname stream))
    (assert (= host test-host) "localname host server")
    (assert (= port (scan-number test-port)) "localname port server")))

# Test localname and peername
# 077bf5eba
(repeat 10
  (with [s (net/server test-host test-port names-handler)]
    (repeat 10
      (with [conn (net/connect test-host test-port)]
        (def [host port] (net/peername conn))
        (assert (= host test-host) "peername host client ")
        (assert (= port (scan-number test-port)) "peername port client")
        # let server close
        (ev/write conn " "))))
  (gccollect))

# Create pipe
# 12f09ad2d
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

# cff52ded5
(var result nil)
(var fiber nil)
(set fiber
  (ev/spawn
    (set result (protect (ev/sleep 10)))
    (assert (= result '(false "boop")) "ev/cancel 1")))
(ev/sleep 0)
(ev/cancel fiber "boop")

# f0dbc2e
(assert (os/execute [;run janet "-e" `(+ 1 2 3)`] :xp) "os/execute self")

# Test some channel
# e76b8da26
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
# 868cdb9
(def ch (ev/thread-chan 2))
(def att (ev/thread-chan 109))
(assert att "`att` was nil after creation")
(ev/give ch att)
(ev/do-thread
  (assert (ev/take ch)
          "channel packing bug for threaded abstracts on threaded channels."))

# marshal channels
# 76be8006a
(def ch (ev/chan 10))
(ev/give ch "hello")
(ev/give ch "world")
(def ch2 (-> ch marshal unmarshal))
(def item1 (ev/take ch2))
(def item2 (ev/take ch2))
(assert (= item1 "hello"))
(assert (= item2 "world"))

# ev/take, suspended, channel closed
(def ch (ev/chan))
(ev/go |(ev/chan-close ch))
(assert (= (ev/take ch) nil))

# ev/give, suspended, channel closed
(def ch (ev/chan))
(ev/go |(ev/chan-close ch))
(assert (= (ev/give ch 1) nil))

# ev/select, suspended take operation, channel closed
(def ch (ev/chan))
(ev/go |(ev/chan-close ch))
(assert (= (ev/select ch) [:close ch]))

# ev/select, suspended give operation, channel closed
(def ch (ev/chan))
(ev/go |(ev/chan-close ch))
(assert (= (ev/select [ch 1]) [:close ch]))

# ev/gather check
(defn exec-slurp
  "Read stdout of subprocess and return it trimmed in a string."
  [& args]
  (def env (os/environ))
  (put env :out :pipe)
  (def proc (os/spawn args :epx env))
  (def out (get proc :out))
  (def buf @"")
  (ev/gather
    (:read out :all buf)
    (:wait proc))
  (string/trimr buf))
(assert-no-error
  "ev/with-deadline 1"
  (assert (= "hi"
             (ev/with-deadline
               10
               (exec-slurp ;run janet "-e" "(print :hi)")))
          "exec-slurp 1"))

# valgrind-able check for #1337
(def superv (ev/chan 10))
(def f (ev/go |(ev/sleep 1e9) nil superv))
(ev/cancel f (gensym))
(ev/take superv)

# Chat server test
(def conmap @{})

(defn broadcast [em msg]
  (eachk par conmap
         (if (not= par em)
           (if-let [tar (get conmap par)]
             (net/write tar (string/format "[%s]:%s" em msg))))))

(defn handler
  [connection]
  (net/write connection "Whats your name?\n")
  (def name (string/trim (string (ev/read connection 100))))
  (if (get conmap name)
    (do
      (net/write connection "Name already taken!")
      (:close connection))
    (do
      (put conmap name connection)
      (net/write connection (string/format "Welcome %s\n" name))
      (defer (do
               (put conmap name nil)
               (:close connection))
        (while (def msg (ev/read connection 100))
          (broadcast name (string msg)))))))

# Now launch the chat server
(def chat-server (net/listen test-host test-port))
(ev/spawn
    (forever
      (def [ok connection] (protect (net/accept chat-server)))
      (if (and ok connection)
        (ev/call handler connection)
        (break))))

# Make sure we can't bind again with no-reuse
(assert-error "no-reuse"
              (net/listen test-host test-port :stream true))

# Read from socket

(defn expect-read
  [stream text]
  (def result (string (net/read stream 100)))
  (assert (= result text) (string/format "expected %v, got %v" text result)))

# Now do our telnet chat
(def bob (net/connect test-host test-port :stream))
(expect-read bob "Whats your name?\n")
(if (= :mingw (os/which))
  (net/write bob "bob")
  (do
    (def fbob (ev/to-file bob))
    (file/write fbob "bob")
    (file/flush fbob)
    (:close fbob)))
(expect-read bob "Welcome bob\n")
(def alice (net/connect test-host test-port))
(expect-read alice "Whats your name?\n")
(net/write alice "alice")
(expect-read alice "Welcome alice\n")

# Bob says hello, alice gets the message
(net/write bob "hello\n")
(expect-read alice "[bob]:hello\n")

# Alice says hello, bob gets the message
(net/write alice "hi\n")
(expect-read bob "[alice]:hi\n")

# Ted joins the chat server
(def ted (net/connect test-host test-port))
(expect-read ted "Whats your name?\n")
(net/write ted "ted")
(expect-read ted "Welcome ted\n")

# Ted says hi, alice and bob get message
(net/write ted "hi\n")
(expect-read alice "[ted]:hi\n")
(expect-read bob "[ted]:hi\n")

# Bob leaves for work. Now it's just ted and alice
(:close bob)

# Alice messages ted, ted gets message
(net/write alice "wuzzup\n")
(expect-read ted "[alice]:wuzzup\n")
(net/write ted "not much\n")
(expect-read alice "[ted]:not much\n")

# Alice bounces
(:close alice)

# Ted can send messages, nobody gets them :(
(net/write ted "hello?\n")
(:close ted)

# Close chat server
(:close chat-server)

# Issue #1531
(def c (ev/chan 0))
(ev/spawn (while (def x (ev/take c))))
(defn print-to-chan [x] (ev/give c x))
(assert-error "coerce await inside janet_call to error"
              (with-dyns [*out* print-to-chan]
                (pp :foo)))
(ev/chan-close c)

(end-suite)
