/*
* Copyright (c) 2020 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#endif

#ifdef JANET_NET

#ifdef JANET_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "Advapi32.lib")
#else
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#endif

/*
 * Streams - simple abstract type that wraps a pollable + extra flags
 */

#define JANET_STREAM_READABLE 0x200
#define JANET_STREAM_WRITABLE 0x400
#define JANET_STREAM_ACCEPTABLE 0x800
#define JANET_STREAM_UDPSERVER 0x1000

static int janet_stream_close(void *p, size_t s);
static int janet_stream_mark(void *p, size_t s);
static int janet_stream_getter(void *p, Janet key, Janet *out);
static const JanetAbstractType StreamAT = {
    "core/stream",
    janet_stream_close,
    janet_stream_mark,
    janet_stream_getter,
    JANET_ATEND_GET
};

typedef JanetPollable JanetStream;

static const JanetAbstractType AddressAT = {
    "core/socket-address",
    JANET_ATEND_NAME
};

#ifdef JANET_WINDOWS
#define JSOCKCLOSE(x) closesocket(x)
#define JSOCKDEFAULT INVALID_SOCKET
#define JLASTERR WSAGetLastError()
#define JSOCKVALID(x) ((x) != INVALID_SOCKET)
#define JEINTR WSAEINTR
#define JEWOULDBLOCK WSAEWOULDBLOCK
#define JEAGAIN WSAEWOULDBLOCK
#define JPOLL WSAPoll
#define JSock SOCKET
#define JReadInt long
#define JSOCKFLAGS 0
static JanetStream *make_stream(SOCKET fd, uint32_t flags) {
    u_long iMode = 0;
    JanetStream *stream = janet_abstract(&StreamAT, sizeof(JanetStream));
    janet_pollable_init(stream, fd);
    ioctlsocket(fd, FIONBIO, &iMode);
    stream->flags = flags;
    return stream;
}
#else
#define JSOCKCLOSE(x) close(x)
#define JSOCKDEFAULT 0
#define JLASTERR errno
#define JSOCKVALID(x) ((x) >= 0)
#define JEINTR EINTR
#define JEWOULDBLOCK EWOULDBLOCK
#define JEAGAIN EAGAIN
#define JPOLL poll
#define JSock int
#define JReadInt ssize_t
#ifdef SOCK_CLOEXEC
#define JSOCKFLAGS SOCK_CLOEXEC
#else
#define JSOCKFLAGS 0
#endif
static JanetStream *make_stream(int fd, uint32_t flags) {
    JanetStream *stream = janet_abstract(&StreamAT, sizeof(JanetStream));
    janet_pollable_init(stream, fd);
#if !defined(SOCK_CLOEXEC) && defined(O_CLOEXEC)
    int extra = O_CLOEXEC;
#else
    int extra = 0;
#endif
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK | extra);
    stream->flags = flags;
    return stream;
}
#endif

/* We pass this flag to all send calls to prevent sigpipe */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int janet_stream_close(void *p, size_t s) {
    (void) s;
    JanetStream *stream = p;
    if (!(stream->flags & JANET_POLL_FLAG_CLOSED)) {
        JSOCKCLOSE(stream->handle);
        janet_pollable_deinit(stream);
    }
    return 0;
}

static int janet_stream_mark(void *p, size_t s) {
    (void) s;
    janet_pollable_mark((JanetPollable *) p);
    return 0;
}

/*
 * State machine for read
 */

typedef struct {
    JanetListenerState head;
    int32_t bytes_left;
    JanetBuffer *buf;
    int is_chunk;
    int is_recv_from;
} NetStateRead;

JanetAsyncStatus net_machine_read(JanetListenerState *s, JanetAsyncEvent event) {
    NetStateRead *state = (NetStateRead *) s;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(janet_wrap_buffer(state->buf));
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            /* Read is finished, even if chunk is incomplete */
            janet_schedule(s->fiber, janet_wrap_nil());
            return JANET_ASYNC_STATUS_DONE;
        case JANET_ASYNC_EVENT_READ:
            /* Read in bytes */
        {
            JanetBuffer *buffer = state->buf;
            int32_t bytes_left = state->bytes_left;
            janet_buffer_extra(buffer, bytes_left);
            JReadInt nread;
            char saddr[256];
            socklen_t socklen = sizeof(saddr);
            do {
                if (state->is_recv_from) {
                    nread = recvfrom(s->pollable->handle, buffer->data + buffer->count, bytes_left, 0,
                                     (struct sockaddr *)&saddr, &socklen);
                } else {
                    nread = recv(s->pollable->handle, buffer->data + buffer->count, bytes_left, 0);
                }
            } while (nread == -1 && JLASTERR == JEINTR);
            if (JLASTERR == JEAGAIN || JLASTERR == JEWOULDBLOCK) {
                break;
            }

            /* Increment buffer counts */
            if (nread > 0) {
                buffer->count += nread;
                bytes_left -= nread;
            } else {
                bytes_left = 0;
            }
            state->bytes_left = bytes_left;

            /* Resume if done */
            if (!state->is_chunk || bytes_left == 0) {
                Janet resume_val;
                if (state->is_recv_from) {
                    void *abst = janet_abstract(&AddressAT, socklen);
                    memcpy(abst, &saddr, socklen);
                    resume_val = janet_wrap_abstract(abst);
                } else {
                    resume_val = nread > 0 ? janet_wrap_buffer(buffer) : janet_wrap_nil();
                }
                janet_schedule(s->fiber, resume_val);
                return JANET_ASYNC_STATUS_DONE;
            }
        }
        break;
    }
    return JANET_ASYNC_STATUS_NOT_DONE;
}

JANET_NO_RETURN static void janet_sched_read(JanetStream *stream, JanetBuffer *buf, int32_t nbytes) {
    NetStateRead *state = (NetStateRead *) janet_listen(stream, net_machine_read,
                          JANET_ASYNC_LISTEN_READ, sizeof(NetStateRead));
    state->is_chunk = 0;
    state->buf = buf;
    state->bytes_left = nbytes;
    state->is_recv_from = 0;
    janet_await();
}

JANET_NO_RETURN static void janet_sched_chunk(JanetStream *stream, JanetBuffer *buf, int32_t nbytes) {
    NetStateRead *state = (NetStateRead *) janet_listen(stream, net_machine_read,
                          JANET_ASYNC_LISTEN_READ, sizeof(NetStateRead));
    state->is_chunk = 1;
    state->buf = buf;
    state->bytes_left = nbytes;
    state->is_recv_from = 0;
    janet_await();
}

JANET_NO_RETURN static void janet_sched_recv_from(JanetStream *stream, JanetBuffer *buf, int32_t nbytes) {
    NetStateRead *state = (NetStateRead *) janet_listen(stream, net_machine_read,
                          JANET_ASYNC_LISTEN_READ, sizeof(NetStateRead));
    state->is_chunk = 0;
    state->buf = buf;
    state->bytes_left = nbytes;
    state->is_recv_from = 1;
    janet_await();
}

/*
 * State machine for write/send-to
 */

typedef struct {
    JanetListenerState head;
    union {
        JanetBuffer *buf;
        const uint8_t *str;
    } src;
    int32_t start;
    int is_buffer;
    void *dest_abst;
} NetStateWrite;

JanetAsyncStatus net_machine_write(JanetListenerState *s, JanetAsyncEvent event) {
    NetStateWrite *state = (NetStateWrite *) s;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(state->is_buffer
                       ? janet_wrap_buffer(state->src.buf)
                       : janet_wrap_string(state->src.str));
            if (state->dest_abst != NULL) {
                janet_mark(janet_wrap_abstract(state->dest_abst));
            }
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_schedule(s->fiber, janet_wrap_nil());
            return JANET_ASYNC_STATUS_DONE;
        case JANET_ASYNC_EVENT_WRITE: {
            int32_t start, len;
            const uint8_t *bytes;
            start = state->start;
            if (state->is_buffer) {
                JanetBuffer *buffer = state->src.buf;
                bytes = buffer->data;
                len = buffer->count;
            } else {
                bytes = state->src.str;
                len = janet_string_length(bytes);
            }
            if (start < len) {
                int32_t nbytes = len - start;
                JReadInt nwrote;
                do {
                    void *dest_abst = state->dest_abst;
                    if (dest_abst) {
                        nwrote = sendto(s->pollable->handle, bytes + start, nbytes, 0,
                                        (struct sockaddr *) dest_abst, janet_abstract_size(dest_abst));
                    } else {
                        nwrote = send(s->pollable->handle, bytes + start, nbytes, MSG_NOSIGNAL);
                    }
                } while (nwrote == -1 && JLASTERR == JEINTR);
                if (nwrote > 0) {
                    start += nwrote;
                } else {
                    start = len;
                }
            }
            state->start = start;
            if (start >= len) {
                janet_schedule(s->fiber, janet_wrap_nil());
                return JANET_ASYNC_STATUS_DONE;
            }
            break;
        }
        break;
    }
    return JANET_ASYNC_STATUS_NOT_DONE;
}

JANET_NO_RETURN static void janet_sched_write_buffer(JanetStream *stream, JanetBuffer *buf, void *dest_abst) {
    NetStateWrite *state = (NetStateWrite *) janet_listen(stream, net_machine_write,
                           JANET_ASYNC_LISTEN_WRITE, sizeof(NetStateWrite));
    state->is_buffer = 1;
    state->start = 0;
    state->src.buf = buf;
    state->dest_abst = dest_abst;
    janet_await();
}


JANET_NO_RETURN static void janet_sched_write_stringlike(JanetStream *stream, const uint8_t *str, void *dest_abst) {
    NetStateWrite *state = (NetStateWrite *) janet_listen(stream, net_machine_write,
                           JANET_ASYNC_LISTEN_WRITE, sizeof(NetStateWrite));
    state->is_buffer = 0;
    state->start = 0;
    state->src.str = str;
    state->dest_abst = dest_abst;
    janet_await();
}

/*
 * State machine for simple server
 */

typedef struct {
    JanetListenerState head;
    JanetFunction *function;
} NetStateSimpleServer;

JanetAsyncStatus net_machine_simple_server(JanetListenerState *s, JanetAsyncEvent event) {
    NetStateSimpleServer *state = (NetStateSimpleServer *) s;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_INIT:
            /* We know the pollable will be a stream */
            janet_gcroot(janet_wrap_abstract(s->pollable));
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(janet_wrap_function(state->function));
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_schedule(s->fiber, janet_wrap_nil());
            janet_gcunroot(janet_wrap_abstract(s->pollable));
            return JANET_ASYNC_STATUS_DONE;
        case JANET_ASYNC_EVENT_READ: {
            JSock connfd = accept(s->pollable->handle, NULL, NULL);
            if (JSOCKVALID(connfd)) {
                /* Made a new connection socket */
                JanetStream *stream = make_stream(connfd, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
                Janet streamv = janet_wrap_abstract(stream);
                JanetFiber *fiber = janet_fiber(state->function, 64, 1, &streamv);
                janet_schedule(fiber, janet_wrap_nil());
            }
            break;
        }
    }
    return JANET_ASYNC_STATUS_NOT_DONE;
}

/* State machine for accepting connections. */

typedef struct {
    JanetListenerState head;
} NetStateAccept;

JanetAsyncStatus net_machine_accept(JanetListenerState *s, JanetAsyncEvent event) {
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_schedule(s->fiber, janet_wrap_nil());
            return JANET_ASYNC_STATUS_DONE;
        case JANET_ASYNC_EVENT_READ: {
            JSock connfd = accept(s->pollable->handle, NULL, NULL);
            if (JSOCKVALID(connfd)) {
                JanetStream *stream = make_stream(connfd, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
                Janet streamv = janet_wrap_abstract(stream);
                janet_schedule(s->fiber, streamv);
                return JANET_ASYNC_STATUS_DONE;
            }
            break;
        }
    }
    return JANET_ASYNC_STATUS_NOT_DONE;
}

JANET_NO_RETURN static void janet_sched_accept(JanetStream *stream) {
    janet_listen(stream, net_machine_accept, JANET_ASYNC_LISTEN_READ, sizeof(NetStateAccept));
    janet_await();
}

/* Adress info */

static int janet_get_sockettype(Janet *argv, int32_t argc, int32_t n) {
    JanetKeyword stype = janet_optkeyword(argv, argc, n, NULL);
    int socktype = SOCK_DGRAM;
    if ((NULL == stype) || !janet_cstrcmp(stype, "stream")) {
        socktype = SOCK_STREAM;
    } else if (janet_cstrcmp(stype, "datagram")) {
        janet_panicf("expected socket type as :stream or :datagram, got %v", argv[n]);
    }
    return socktype;
}

/* Needs argc >= offset + 2 */
/* For unix paths, just rertuns a single sockaddr and sets *is_unix to 1, otherwise 0 */
static struct addrinfo *janet_get_addrinfo(Janet *argv, int32_t offset, int socktype, int passive, int *is_unix) {
    /* Unix socket support */
    if (janet_keyeq(argv[offset], "unix")) {
        const char *path = janet_getcstring(argv, offset + 1);
        struct sockaddr_un *saddr = malloc(sizeof(struct sockaddr_un));
        if (saddr == NULL) {
            JANET_OUT_OF_MEMORY;
        }
        saddr->sun_family = AF_UNIX;
        snprintf(saddr->sun_path, 108, "%s", path);
        *is_unix = 1;
        return (struct addrinfo *) saddr;
    }
    /* Get host and port */
    const char *host = janet_getcstring(argv, offset);
    const char *port;
    if (janet_checkint(argv[offset + 1])) {
        port = (const char *)janet_to_string(argv[offset + 1]);
    } else {
        port = janet_optcstring(argv, offset + 2, offset + 1, NULL);
    }
    /* getaddrinfo */
    struct addrinfo *ai = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    int status = getaddrinfo(host, port, &hints, &ai);
    if (status) {
        janet_panicf("could not get address info: %s", gai_strerror(status));
    }
    *is_unix = 0;
    return ai;
}

/*
 * C Funs
 */

static Janet cfun_net_sockaddr(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 4);
    int socktype = janet_get_sockettype(argv, argc, 2);
    int is_unix = 0;
    int make_arr = (argc >= 3 && janet_truthy(argv[3]));
    struct addrinfo *ai = janet_get_addrinfo(argv, 0, socktype, 0, &is_unix);
    if (is_unix) {
        void *abst = janet_abstract(&AddressAT, sizeof(struct sockaddr_un));
        memcpy(abst, ai, sizeof(struct sockaddr_un));
        Janet ret = janet_wrap_abstract(abst);
        return make_arr ? janet_wrap_array(janet_array_n(&ret, 1)) : ret;
    }
    if (make_arr) {
        /* Select all */
        JanetArray *arr = janet_array(10);
        struct addrinfo *iter = ai;
        while (NULL != iter) {
            void *abst = janet_abstract(&AddressAT, iter->ai_addrlen);
            memcpy(abst, iter->ai_addr, iter->ai_addrlen);
            janet_array_push(arr, janet_wrap_abstract(abst));
            iter = iter->ai_next;
        }
        freeaddrinfo(ai);
        return janet_wrap_array(arr);
    } else {
        /* Select first */
        if (NULL == ai) {
            janet_panic("no data for given address");
        }
        void *abst = janet_abstract(&AddressAT, ai->ai_addrlen);
        memcpy(abst, ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(ai);
        return janet_wrap_abstract(abst);
    }
}

static Janet cfun_net_connect(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);

    int socktype = janet_get_sockettype(argv, argc, 2);
    int is_unix = 0;
    struct addrinfo *ai = janet_get_addrinfo(argv, 0, socktype, 0, &is_unix);

    /* Create socket */
    JSock sock = JSOCKDEFAULT;
    void *addr = NULL;
    socklen_t addrlen;
    if (is_unix) {
        sock = socket(AF_UNIX, socktype | JSOCKFLAGS, 0);
        if (!JSOCKVALID(sock)) {
            janet_panic("could not create socket");
        }
        addr = (void *) ai;
        addrlen = sizeof(struct sockaddr_un);
    } else {
        struct addrinfo *rp = NULL;
        for (rp = ai; rp != NULL; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype | JSOCKFLAGS, rp->ai_protocol);
            if (JSOCKVALID(sock)) {
                addr = rp->ai_addr;
                addrlen = rp->ai_addrlen;
                break;
            }
        }
        if (NULL == addr) {
            freeaddrinfo(ai);
            janet_panic("could not create socket");
        }
    }

    /* Connect to socket */
    int status = connect(sock, addr, addrlen);
    if (is_unix) {
        free(ai);
    } else {
        freeaddrinfo(ai);
    }

    if (status == -1) {
        JSOCKCLOSE(sock);
        janet_panic("could not connect to socket");
    }

    /* Wrap socket in abstract type JanetStream */
    JanetStream *stream = make_stream(sock, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
    return janet_wrap_abstract(stream);
}

static const char *serverify_socket(JSock sfd) {
    /* Set various socket options */
    int enable = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable, sizeof(int)) < 0) {
        return "setsockopt(SO_REUSEADDR) failed";
    }
#ifdef SO_NOSIGPIPE
    if (setsockopt(sfd, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(int)) < 0) {
        return "setsockopt(SO_NOSIGPIPE) failed";
    }
#endif
#ifdef SO_REUSEPORT
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
        return "setsockopt(SO_REUSEPORT) failed";
    }
#endif
    return NULL;
}

static Janet cfun_net_server(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 4);

    /* Get host, port, and handler*/
    JanetFunction *fun = janet_optfunction(argv, argc, 2, NULL);

    int socktype = janet_get_sockettype(argv, argc, 3);
    int is_unix = 0;
    struct addrinfo *ai = janet_get_addrinfo(argv, 0, socktype, 1, &is_unix);

    JSock sfd = JSOCKDEFAULT;
    if (is_unix) {
        sfd = socket(AF_UNIX, socktype | JSOCKFLAGS, 0);
        if (!JSOCKVALID(sfd)) {
            free(ai);
            janet_panic("could not create socket");
        }
        const char *err = serverify_socket(sfd);
        if (NULL != err || bind(sfd, (struct sockaddr *)ai, sizeof(struct sockaddr_un))) {
            JSOCKCLOSE(sfd);
            free(ai);
            janet_panic(err ? err : "could not bind socket");
        }
        free(ai);
    } else {
        /* Check all addrinfos in a loop for the first that we can bind to. */
        struct addrinfo *rp = NULL;
        for (rp = ai; rp != NULL; rp = rp->ai_next) {
            sfd = socket(rp->ai_family, rp->ai_socktype | JSOCKFLAGS, rp->ai_protocol);
            if (!JSOCKVALID(sfd)) continue;
            const char *err = serverify_socket(sfd);
            if (NULL != err) {
                JSOCKCLOSE(sfd);
                continue;
            }
            /* Bind */
            if (bind(sfd, rp->ai_addr, (int) rp->ai_addrlen) == 0) break;
            JSOCKCLOSE(sfd);
        }
        freeaddrinfo(ai);
        if (NULL == rp) {
            janet_panic("could not bind to any sockets");
        }
    }

    if (socktype == SOCK_DGRAM) {
        /* Datagram server (UDP) */

        if (NULL == fun) {
            /* Server no handler */
            JanetStream *stream = make_stream(sfd, JANET_STREAM_UDPSERVER | JANET_STREAM_READABLE);
            return janet_wrap_abstract(stream);
        } else {
            /* Server with handler */
            janet_panic("handler must be nil for datagram server");
        }
    } else {
        /* Stream server (TCP) */

        /* listen */
        int status = listen(sfd, 1024);
        if (status) {
            JSOCKCLOSE(sfd);
            janet_panic("could not listen on file descriptor");
        }

        /* Put sfd on our loop */
        if (NULL == fun) {
            JanetStream *stream = make_stream(sfd, JANET_STREAM_ACCEPTABLE);
            return janet_wrap_abstract(stream);
        } else {
            /* Server with handler */
            JanetStream *stream = make_stream(sfd, 0);
            NetStateSimpleServer *ss = (NetStateSimpleServer *) janet_listen(stream, net_machine_simple_server,
                                       JANET_ASYNC_LISTEN_READ, sizeof(NetStateSimpleServer));
            ss->function = fun;
            return janet_wrap_abstract(stream);
        }
    }
}

static void check_stream_flag(JanetStream *stream, int flag) {
    if (!(stream->flags & flag) || (stream->flags & JANET_POLL_FLAG_CLOSED)) {
        const char *msg = "";
        if (flag == JANET_STREAM_READABLE) msg = "readable";
        if (flag == JANET_STREAM_WRITABLE) msg = "writable";
        if (flag == JANET_STREAM_ACCEPTABLE) msg = "server";
        if (flag == JANET_STREAM_UDPSERVER) msg = "datagram server";
        janet_panicf("bad stream, expected %s stream", msg);
    }
}

static Janet cfun_stream_accept(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_ACCEPTABLE);
    janet_sched_accept(stream);
}

static Janet cfun_stream_read(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_READABLE);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    janet_sched_read(stream, buffer, n);
}

static Janet cfun_stream_chunk(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_READABLE);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    janet_sched_chunk(stream, buffer, n);
}

static Janet cfun_stream_recv_from(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_UDPSERVER);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_getbuffer(argv, 2);
    janet_sched_recv_from(stream, buffer, n);
}

static Janet cfun_stream_close(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    janet_stream_close(stream, 0);
    return janet_wrap_nil();
}

static Janet cfun_stream_write(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_WRITABLE);
    if (janet_checktype(argv[1], JANET_BUFFER)) {
        janet_sched_write_buffer(stream, janet_getbuffer(argv, 1), NULL);
    } else {
        JanetByteView bytes = janet_getbytes(argv, 1);
        janet_sched_write_stringlike(stream, bytes.bytes, NULL);
    }
}

static Janet cfun_stream_send_to(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_UDPSERVER);
    void *dest = janet_getabstract(argv, 1, &AddressAT);
    if (janet_checktype(argv[2], JANET_BUFFER)) {
        janet_sched_write_buffer(stream, janet_getbuffer(argv, 2), dest);
    } else {
        JanetByteView bytes = janet_getbytes(argv, 2);
        janet_sched_write_stringlike(stream, bytes.bytes, dest);
    }
}

static Janet cfun_stream_flush(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    check_stream_flag(stream, JANET_STREAM_WRITABLE);
    /* Toggle no delay flag */
    int flag = 1;
    setsockopt(stream->handle, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
    flag = 0;
    setsockopt(stream->handle, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
    return argv[0];
}

static const JanetMethod stream_methods[] = {
    {"chunk", cfun_stream_chunk},
    {"close", cfun_stream_close},
    {"read", cfun_stream_read},
    {"write", cfun_stream_write},
    {"flush", cfun_stream_flush},
    {"accept", cfun_stream_accept},
    {"send-to", cfun_stream_send_to},
    {"recv-from", cfun_stream_recv_from},
    {NULL, NULL}
};

static int janet_stream_getter(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), stream_methods, out);
}

static const JanetReg net_cfuns[] = {
    {
        "net/address", cfun_net_sockaddr,
        JDOC("(net/address host port &opt type)\n\n"
             "Look up the connection information for a given hostname, port, and connection type. Returns "
             "a handle that can be used to send datagrams over network without establishing a connection.")
    },
    {
        "net/server", cfun_net_server,
        JDOC("(net/server host port &opt handler type)\n\n"
             "Start a TCP server. handler is a function that will be called with a stream "
             "on each connection to the server. Returns a new stream that is neither readable nor "
             "writeable. If handler is nil or not provided, net/accept must be used to get the next connection "
             "to the server. The type parameter specifies the type of network connection, either "
             "a stream (usually tcp), or datagram (usually udp). If not specified, the default is "
             "stream.")
    },
    {
        "net/accept", cfun_stream_accept,
        JDOC("(net/accept stream)\n\n"
             "Get the next connection on a server stream. This would usually be called in a loop in a dedicated fiber. "
             "Returns a new duplex stream which represents a connection to the client.")
    },
    {
        "net/read", cfun_stream_read,
        JDOC("(net/read stream nbytes &opt buf)\n\n"
             "Read up to n bytes from a stream, suspending the current fiber until the bytes are available. "
             "If less than n bytes are available (and more than 0), will push those bytes and return early. "
             "Returns a buffer with up to n more bytes in it.")
    },
    {
        "net/chunk", cfun_stream_chunk,
        JDOC("(net/chunk stream nbytes &opt buf)\n\n"
             "Same a net/read, but will wait for all n bytes to arrive rather than return early.")
    },
    {
        "net/write", cfun_stream_write,
        JDOC("(net/write stream data)\n\n"
             "Write data to a stream, suspending the current fiber until the write "
             "completes. Returns stream.")
    },
    {
        "net/send-to", cfun_stream_send_to,
        JDOC("(net/send-to stream dest data)\n\n"
             "Writes a datagram to a server stream. dest is a the destination address of the packet. "
             "Returns stream.")
    },
    {
        "net/recv-from", cfun_stream_recv_from,
        JDOC("(net/recv-from stream nbytes buf)\n\n"
             "Receives data from a server stream and puts it into a buffer. Returns the socket-address the "
             "packet came from.")
    },
    {
        "net/flush", cfun_stream_flush,
        JDOC("(net/flush stream)\n\n"
             "Make sure that a stream is not buffering any data. This temporarily disables Nagle's algorithm. "
             "Use this to make sure data is sent without delay. Returns stream.")
    },
    {
        "net/close", cfun_stream_close,
        JDOC("(net/close stream)\n\n"
             "Close a stream so that no further communication can occur.")
    },
    {
        "net/connect", cfun_net_connect,
        JDOC("(net/connect host porti &opt type)\n\n"
             "Open a connection to communicate with a server. Returns a duplex stream "
             "that can be used to communicate with the server. Type is an optional keyword "
             "to specify a connection type, either :stream or :datagram. The default is :stream. ")
    },
    {NULL, NULL, NULL}
};

void janet_lib_net(JanetTable *env) {
    janet_core_cfuns(env, NULL, net_cfuns);
}

void janet_net_init(void) {
#ifdef JANET_WINDOWS
    WSADATA wsaData;
    janet_assert(!WSAStartup(MAKEWORD(2, 2), &wsaData), "could not start winsock");
#endif
}

void janet_net_deinit(void) {
#ifdef JANET_WINDOWS
    WSACleanup();
#endif
}

#endif
