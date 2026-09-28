# Copyright (c) 2026 Calvin Rose & contributors
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

# Issue #1629
(def thread-channel (ev/thread-chan 100))
(def super (ev/thread-chan 10))
(defn worker []
  (while true
    (def item (ev/take thread-channel))
    (when (= item :deadline)
      (ev/deadline 0.1 nil (fiber/current) true))))
(ev/thread worker nil :n super)
(ev/give thread-channel :item)
(ev/sleep 0.05)
(ev/give thread-channel :item)
(ev/sleep 0.05)
(ev/give thread-channel :deadline)
(ev/sleep 0.05)
(ev/give thread-channel :item)
(ev/sleep 0.05)
(ev/give thread-channel :item)
(ev/sleep 0.15)
(assert (deep= '(:error "deadline expired" nil) (ev/take super)) "deadline expirataion")

# Another variant
(def thread-channel :shadow (ev/thread-chan 100))
(def super :shadow (ev/thread-chan 10))
(defn worker :shadow []
  (while true
    (def item (ev/take thread-channel))
    (when (= item :deadline)
      (ev/deadline 0.1))))
(ev/thread worker nil :n super)
(ev/give thread-channel :deadline)
(ev/sleep 0.2)
(assert (deep= '(:error "deadline expired" nil) (ev/take super)) "deadline expirataion")

# Issue #1705 - ev select
(def supervisor (ev/chan 10))

(def ch (ev/chan))
(def ch2 (ev/chan))

(ev/go |(do
          (ev/select ch ch2)
          (:close ch)
          "close ch...")
       nil supervisor)

(ev/go |(do
          (ev/sleep 0.05)
          (:close ch2)
          "close ch2...")
       nil supervisor)

(assert (let [[status] (ev/take supervisor)] (= status :ok)) "status 1 ev/select")
(assert (let [[status] (ev/take supervisor)] (= status :ok)) "status 2 ev/select")
(ev/sleep 0.1) # can we do better?
(assert (= 0 (ev/count supervisor)) "empty supervisor")

# Issue #1707
(def f (coro (repeat 10 (yield 1))))
(resume f)
(assert-error "cannot schedule non-new fiber"
              (ev/go f))

# IO file copying
(os/mkdir "tmp")
(def f-original (file/open "tmp/out.txt" :wb))
(xprin f-original "hello\n")
(file/flush f-original)
(ev/do-thread
  # Closes a COPY of the original file, otherwise we get a user-after-close file descriptor
  (:close f-original))
(def g-original (file/open "tmp/out2.txt" :wb))
(xprin g-original "world1\n")
(xprin f-original "world2\n")
(:close f-original)
(xprin g-original "abc\n")
(:close g-original)
(assert (deep= @"hello\nworld2\n" (slurp "tmp/out.txt")) "file threading 1")
(assert (deep= @"world1\nabc\n" (slurp "tmp/out2.txt")) "file threading 2")

# A value given to a threaded channel whose only waiting reader has since been
# resumed by another select clause must be kept for the next reader.
(def stale-tchan (ev/thread-chan 10))
(def stale-pchan (ev/chan 10))
(ev/spawn (ev/select stale-tchan stale-pchan))
(ev/sleep 0)
(ev/give stale-pchan 1)
(ev/sleep 0)
(ev/give stale-tchan 42)
(ev/sleep 0.01)
(assert (= 42 (try (ev/with-deadline 1 (ev/take stale-tchan)) ([_] nil)))
        "value given to a threaded channel with a stale reader is kept")

# Fibers that parked on a threaded channel must not stay gc roots forever.
# Leaked roots also made every nested resume slower (roots are scanned on unroot).
(defn- count-uncollected [n park wake]
  (def fibers (table/weak-keys n))
  (repeat n
    (def fiber (ev/spawn (park)))
    (ev/sleep 0)
    (wake)
    (ev/sleep 0)
    (put fibers fiber true))
  (gccollect)
  (length fibers))
(def tchan (ev/thread-chan 10))
(def pchan (ev/chan 10))
(assert (>= 1 (count-uncollected 100 |(ev/select tchan pchan) |(ev/give pchan 1)))
        "no leaked roots for ev/select woken by another channel")
(assert (>= 1 (count-uncollected 100 |(ev/take tchan) |(ev/give tchan 1)))
        "no leaked roots for ev/take on threaded channel")

# ev/select must not lose an item that a thread gives to a threaded channel
# between the select's check of that channel and its wait on it. Before, the
# item was taken and dropped, and the select never waited on that channel.
(def race-n 5000)
(def race-tchan (ev/thread-chan 16))
(def race-pchan (ev/chan))
(ev/thread
  (fn []
    (for i 0 race-n
      (ev/give race-tchan i)
      (var x 0)
      (repeat 500 (++ x)))
    (ev/give race-tchan :done))
  nil :n)
(def race-got @[])
(def race-result
  (try
    (ev/with-deadline 20
      (forever
        (def [_ _ v] (ev/select race-tchan race-pchan))
        (if (= v :done) (break))
        (array/push race-got v))
      :ok)
    ([err] err)))
(assert (= :ok race-result) "ev/select over a threaded channel does not hang")
(assert (= race-n (length race-got)) "ev/select over a threaded channel loses no items")

(end-suite)
