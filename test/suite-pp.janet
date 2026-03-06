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

# Appending buffer to self
# 6b76ac3d1
(with-dyns [:out @""]
  (prin "abcd")
  (prin (dyn :out))
  (prin (dyn :out))
  (assert (deep= (dyn :out) @"abcdabcdabcdabcd") "print buffer to self"))

# Buffer self blitting, check for use after free
# bbcfaf128
(def buf1 @"1234567890")
(buffer/blit buf1 buf1 -1)
(buffer/blit buf1 buf1 -1)
(buffer/blit buf1 buf1 -1)
(buffer/blit buf1 buf1 -1)
(assert (= (string buf1) (string/repeat "1234567890" 16))
        "buffer blit against self")

# Check for bugs with printing self with buffer/format
# bbcfaf128
(def buftemp @"abcd")
(assert (= (string (buffer/format buftemp "---%p---" buftemp))
           `abcd---@"abcd"---`) "buffer/format on self 1")
(def buftemp @"abcd")
(assert (= (string (buffer/format buftemp "---%p %p---" buftemp buftemp))
           `abcd---@"abcd" @"abcd"---`) "buffer/format on self 2")

# 5c364e0
(defn check-jdn [x]
  (assert (deep= (parse (string/format "%j" x)) x) "round trip jdn"))

(check-jdn 0)
(check-jdn nil)
(check-jdn [])
(check-jdn @[[] [] 1231 9.123123 -123123 0.1231231230001])
(check-jdn -0.123123123123)
(check-jdn 12837192371923)
(check-jdn "a string")
(check-jdn @"a buffer")

# Test multiline pretty specifiers
(let [tup [:keyword "string" @"buffer"]
      tab @{true (table/setproto @{:bar tup
                                   :baz 42}
                                 @{:_name "Foo"})}]
  (set (tab tup) tab)
  (assert (= (string/format "%m" {tup @[tup tab]
                                  'symbol tup})
          `
{symbol (:keyword
         "string"
         @"buffer")
 (:keyword
  "string"
  @"buffer") @[(:keyword
                "string"
                @"buffer")
               @{true @Foo{:bar (:keyword
                                 "string"
                                 @"buffer")
                           :baz 42}
                 (:keyword
                  "string"
                  @"buffer") <cycle 2>}]}`))
  (assert (= (string/format "%p" {(freeze (zipcoll (range 42)
                                                   (range -42 0))) tab})
             `
{{0 -42
  1 -41
  2 -40
  3 -39
  4 -38
  5 -37
  6 -36
  7 -35
  8 -34
  9 -33
  10 -32
  11 -31
  12 -30
  13 -29
  14 -28
  15 -27
  16 -26
  17 -25
  18 -24
  19 -23
  20 -22
  21 -21
  22 -20
  23 -19
  24 -18
  25 -17
  26 -16
  27 -15
  28 -14
  29 -13
  ...} @{true @Foo{:bar (:keyword
                         "string"
                         @"buffer")
                   :baz 42}
         (:keyword
          "string"
          @"buffer") <cycle 1>}}`)))
(end-suite)
