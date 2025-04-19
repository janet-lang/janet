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

# Docstring parsing
(assert (deep= (doc-parse "foo")
               '@[(("foo" 3))])
        "doc-parse line")
(assert (deep= (doc-parse "foo bar")
               '@[(("foo" 3) ("bar" 3))])
        "doc-parse multi-word line")
(assert (deep= (doc-parse "foo bar baz qux\n\nfoo bar baz qux")
               '@[(("foo" 3) ("bar" 3) ("baz" 3) ("qux" 3)) (("foo" 3) ("bar" 3) ("baz" 3) ("qux" 3))])
        "doc-parse multiple paragraphs")
(assert (deep= (doc-parse "* foo")
               '@[@[:ul @{} @[(("foo" 3))]]])
        "doc-parse simple ul")
(assert (deep= (doc-parse "* foo\n* bar")
               '@[@[:ul @{} @[(("foo" 3))] @[(("bar" 3))]]])
        "doc-parse multi-item ul")
(assert (deep= (doc-parse "* foo\n\n  bar\n* baz")
               '@[@[:ul @{} @[(("foo" 3)) (("bar" 3))] @[(("baz" 3))]]])
        "doc-parse multi-paragraph ul")
(assert (deep= (doc-parse "* foo\n\n  1. bar\n  2. baz\n* qux")
               '@[@[:ul @{} @[(("foo" 3)) @[:ol @{} @[(("bar" 3))] @[(("baz" 3))]]] @[(("qux" 3))]]])
        "doc-parse nested lists")
(assert (deep= (doc-parse "* foo\n\n* bar")
               '@[@[:ul @{:loose? true} @[(("foo" 3))] @[(("bar" 3))]]])
        "doc-parse loose ul")
(assert (deep= (doc-parse "* foo\n\n* bar\n\nbaz")
               '@[@[:ul @{:loose? true} @[(("foo" 3))] @[(("bar" 3))]] (("baz" 3))])
        "doc-parse loose ul and separate block")
(assert (deep= (doc-parse "* foo\n\n* bar\n\n      baz")
               '@[@[:ul @{:loose? true} @[(("foo" 3))] @[(("bar" 3)) @[:cb "baz"]]]])
        "doc-parse loose ul and combined block")
(assert (deep= (doc-parse "foo _bar_ **baz qux**")
               '@[(("foo" 3) (:underline 0 "_") (:underline 3) ("bar" 3) (:bold 0 "**") ("baz" 3) (:bold 3) ("qux" 3))])
        "doc-parse facets")
(assert (deep= (doc-parse "foo **_bar_ baz qux**")
               '@[(("foo" 3) (:bold 0 "**") (:underline 0 "_") (:underline 3) ("bar" 3) ("baz" 3) (:bold 3) ("qux" 3))])
        "doc-parse nested facets")
(assert (deep= (doc-parse "(doc* &opt sym)")
               '@[(("(doc*" 5) ("&opt" 4) ("sym)" 4))])
        "doc-parse non-markup symbols")

# Docstring formatting
(assert (deep= (doc-format "foo" nil 0)
               @"\nfoo\n")
        "doc-format line")
(assert (deep= (doc-format "* this is a test of wrapping" 30 0)
               @"\n* this is a test of \n  wrapping\n")
        "doc-format wrap lists")
(assert (deep= (doc-format "* foo\n  * bar\n  * baz\n* qux" nil 0)
               @"\n* foo\n  * bar\n  * baz\n* qux\n")
        "doc-format nested lists")
(assert (deep= (doc-format "foo _bar_ **baz qux**" nil 0 false)
               @"\nfoo _bar_ **baz qux**\n")
        "doc-format facets")

(end-suite)

