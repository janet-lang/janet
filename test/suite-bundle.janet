# Copyright (c) 2024 Calvin Rose
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

(assert true) # smoke test

# Testing here is stateful since we are manipulating the filesystem.

# Copy since not exposed in boot.janet
(defn- bundle-rpath
  [path]
  (string/replace-all "\\" "/" (os/realpath path)))

# Test mkdir -> rmdir
(assert (os/mkdir "tempdir123"))
(rmrf "tempdir123")

# Setup a temporary syspath for manipultation
(math/seedrandom (os/cryptorand 16))
(def syspath (randdir))
(rmrf syspath)
(assert (os/mkdir syspath))
(put root-env *syspath* (bundle-rpath syspath))
(unless (os/getenv "VERBOSE")
  (setdyn *out* @""))
(assert (empty? (bundle/list)) "initial bundle/list")
(assert (empty? (bundle/topolist)) "initial bundle/topolist")

# Try (and fail) to install sample-bundle (missing deps)
(assert-error "missing dependencies sample-dep1, sample-dep2"
              (bundle/install "./examples/sample-bundle"))
(assert (empty? (bundle/list)))

# Install deps (dep1 as :auto-remove)
(assert-no-error "sample-dep2"
                 (bundle/install "./examples/sample-dep2"))
(assert (= 1 (length (bundle/list))))
(assert-no-error "sample-dep1" (bundle/install "./examples/sample-dep1"))
(assert (= 2 (length (bundle/list))))

(assert-no-error "sample-dep2 reinstall" (bundle/reinstall "sample-dep2"))
(assert-no-error "sample-dep1 reinstall" (bundle/reinstall "sample-dep1" :auto-remove true))

(assert (= 2 (length (bundle/list))) "bundles are listed correctly 1")
(assert (= 2 (length (bundle/topolist))) "bundles are listed correctly 2")

# Now install sample-bundle
(assert-no-error "sample-bundle install" (bundle/install "./examples/sample-bundle"))

(assert-error "" (bundle/install "./examples/sample-dep11111"))

(assert (= 3 (length (bundle/list))) "bundles are listed correctly 3")
(assert (= 3 (length (bundle/topolist))) "bundles are listed correctly 4")

# Check topolist has not bad order
(def tlist (bundle/topolist))
(assert (> (index-of "sample-bundle" tlist) (index-of "sample-dep2" tlist)) "topolist 1")
(assert (> (index-of "sample-bundle" tlist) (index-of "sample-dep1" tlist)) "topolist 2")
(assert (> (index-of "sample-dep1" tlist) (index-of "sample-dep2" tlist)) "topolist 3")

# Prune should do nothing
(assert-no-error "first prune" (bundle/prune))
(assert (= 3 (length (bundle/list))) "bundles are listed correctly 3")
(assert (= 3 (length (bundle/topolist))) "bundles are listed correctly 4")

# Check that we can import the main dependency
(import mymod)
(assert (= 288 (mymod/myfn 12)) "using sample-bundle")

# Manual uninstall of dep1 and dep2 shouldn't work either since that would break dependencies
(assert-error "cannot uninstall sample-dep1, breaks dependent bundles @[\"sample-bundle\"]"
              (bundle/uninstall "sample-dep1"))

# Check bundle file aliases
(assert-no-error "sample-bundle-aliases install" (bundle/install "./examples/sample-bundle-aliases"))
(assert (= 4 (length (bundle/list))) "bundles are listed correctly 5")
(assert-no-error "import aliases" (import aliases-mod))
(assert (deep= (range 12) (aliases-mod/fun 12)) "using sample-bundle-aliases")
(assert-no-error "aliases uninstall" (bundle/uninstall "sample-bundle-aliases"))

# Now re-install sample-bundle as auto-remove
(assert-no-error "sample-bundle install" (bundle/reinstall "sample-bundle" :auto-remove true))

# Reinstallation should also work without being concerned about breaking dependencies
(assert-no-error "reinstall dep" (bundle/reinstall "sample-dep2"))

# Now prune should get rid of everything except sample-dep2
(assert-no-error "second prune" (bundle/prune))

# Now check that we exactly one package left, which is dep2
(assert (= 1 (length (bundle/list))) "bundles are listed correctly 5")
(assert (= 1 (length (bundle/topolist))) "bundles are listed correctly 6")

# Which we can uninstall manually
(assert-no-error "uninstall dep2" (bundle/uninstall "sample-dep2"))

# Now check bundle listing is again empty
(assert (= 0 (length (bundle/list))) "bundles are listed correctly 7")
(assert (= 0 (length (bundle/topolist))) "bundles are listed correctly 8")

# Try installing a bundle that fails check
(assert-error "bad test" (bundle/install "./examples/sample-bad-bundle" :check true))
(assert (= 0 (length (bundle/list))) "check failure 0")
(assert (= 0 (length (bundle/topolist))) "check failure 1")

(rmrf syspath)

(end-suite)
