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

# Expand on ev testing with some extra network protocol testing.

(import ./helper :prefix "" :exit true)
(start-suite)

# Smoke
(assert true)

# Raw socket testing
(def s (net/socket :datagram :ipv4))
(assert-no-error "multicast ipv4" (net/setsockopt s :ip-multicast-ttl 255))
#(def s6 (net/socket :datagram :ipv6))
#(assert-no-error "multicast ipv6" (net/setsockopt s6 :ipv6-multicast-hops 255))

# Issue #1942 - overflow unix addresses
(compwhen (= :linux (os/which))
  # An abstract name is stored with its exact length, here 2 + 4 bytes.
  (assert (= ["@abc"] (net/address-unpack (net/address :unix "@abc")))
          "abstract unix address unpack")
  # Abstract names longer than sun_path are cut to fit, like ordinary paths,
  # instead of producing an address longer than struct sockaddr_un.
  (def long-name (string "@" (string/repeat "a" 200)))
  (def [unpacked] (net/address-unpack (net/address :unix long-name)))
  (assert (string/has-prefix? unpacked long-name) "long abstract unix address unpack")
  (assert (< (length unpacked) (length long-name)) "long abstract unix address truncated"))

# A datagram from an unbound unix socket carries an empty (Linux) or unnamed
# (macOS) sender address. recv-from hands that back as a short address, which
# should unpack or raise an error without reading past it.
(compwhen (or (= :macos (os/which)) (= :linux (os/which)))
  (def uds-path "./unix-datagram-socket-234")
  (defer (os/rm uds-path)
    (with [server (net/listen :unix uds-path :datagram)]
      (with [client (net/connect :unix uds-path :datagram)]
        (net/write client "hello")
        (def buf @"")
        (def addr (net/recv-from server 16 buf))
        (assert (= "hello" (string buf)) "unix datagram received")
        (def [ok res] (protect (net/address-unpack addr)))
        (assert (if ok (tuple? res) (= "unknown address family" res))
                "unnamed unix datagram sender unpack")))))


(end-suite)
