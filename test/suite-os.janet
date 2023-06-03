# Copyright (c) 2023 Calvin Rose
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

# OS Date test
# 719f7ba0c
(assert (deep= {:year-day 0
                :minutes 30
                :month 0
                :dst false
                :seconds 0
                :year 2014
                :month-day 0
                :hours 20
                :week-day 3}
               (os/date 1388608200)) "os/date")

# OS mktime test
# 3ee43c3ab
(assert (= 1388608200 (os/mktime {:year-day 0
                                  :minutes 30
                                  :month 0
                                  :dst false
                                  :seconds 0
                                  :year 2014
                                  :month-day 0
                                  :hours 20
                                  :week-day 3})) "os/mktime")

(def now (os/time))
(assert (= (os/mktime (os/date now)) now) "UTC os/mktime")
(assert (= (os/mktime (os/date now true) true) now) "local os/mktime")
(assert (= (os/mktime {:year 1970}) 0) "os/mktime default values")

# OS strftime test
# 5cd729c4c
(assert (= (os/strftime "%Y-%m-%d %H:%M:%S" 0) "1970-01-01 00:00:00")
        "strftime UTC epoch")
(assert (= (os/strftime "%Y-%m-%d %H:%M:%S" 1388608200)
           "2014-01-01 20:30:00")
        "strftime january 2014")
(assert (= (try (os/strftime "%%%d%t") ([err] err))
           "invalid conversion specifier '%t'")
        "invalid conversion specifier")

# 07db4c530
(os/setenv "TESTENV1" "v1")
(os/setenv "TESTENV2" "v2")
(assert (= (os/getenv "TESTENV1") "v1") "getenv works")
(def environ (os/environ))
(assert (= [(environ "TESTENV1") (environ "TESTENV2")] ["v1" "v2"])
        "environ works")

# Ensure randomness puts n of pred into our buffer eventually
# 0ac5b243c
(defn cryptorand-check
  [n pred]
  (def max-attempts 10000)
  (var attempts 0)
  (while (not= attempts max-attempts)
    (def cryptobuf (os/cryptorand 10))
    (when (= n (count pred cryptobuf))
      (break))
    (++ attempts))
  (not= attempts max-attempts))

(def v (math/rng-int (math/rng (os/time)) 100))
(assert (cryptorand-check 0 |(= $ v)) "cryptorand skips value sometimes")
(assert (cryptorand-check 1 |(= $ v)) "cryptorand has value sometimes")

(do
  (def buf (buffer/new-filled 1))
  (os/cryptorand 1 buf)
  (assert (= (in buf 0) 0) "cryptorand doesn't overwrite buffer")
  (assert (= (length buf) 2) "cryptorand appends to buffer"))

# 80db68210
(assert-no-error (os/clock :realtime) "realtime clock")
(assert-no-error (os/clock :cputime) "cputime clock")
(assert-no-error (os/clock :monotonic) "monotonic clock")

(def before (os/clock :monotonic))
(def after (os/clock :monotonic))
(assert (>= after before) "monotonic clock is monotonic")

# Perm strings
# a0d61e45d
(assert (= (os/perm-int "rwxrwxrwx") 8r777) "perm 1")
(assert (= (os/perm-int "rwxr-xr-x") 8r755) "perm 2")
(assert (= (os/perm-int "rw-r--r--") 8r644) "perm 3")

(assert (= (band (os/perm-int "rwxrwxrwx") 8r077) 8r077) "perm 4")
(assert (= (band (os/perm-int "rwxr-xr-x") 8r077) 8r055) "perm 5")
(assert (= (band (os/perm-int "rw-r--r--") 8r077) 8r044) "perm 6")

(assert (= (os/perm-string 8r777) "rwxrwxrwx") "perm 7")
(assert (= (os/perm-string 8r755) "rwxr-xr-x") "perm 8")
(assert (= (os/perm-string 8r644) "rw-r--r--") "perm 9")

# os/execute with environment variables
# issue #636 - 7e2c433ab
(assert (= 0 (os/execute [(dyn :executable) "-e" "(+ 1 2 3)"] :pe
                         (merge (os/environ) {"HELLO" "WORLD"})))
        "os/execute with env")

# os/execute regressions
# 427f7c362
(for i 0 10
  (assert (= i (os/execute [(dyn :executable) "-e"
                            (string/format "(os/exit %d)" i)] :p))
          (string "os/execute " i)))

(end-suite)

