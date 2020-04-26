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

#ifdef JANET_WINDOWS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "Advapi32.lib")
#else
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#endif

/*
 * Streams
 */

#define JANET_STREAM_CLOSED 1
#define JANET_STREAM_READABLE 2
#define JANET_STREAM_WRITABLE 4

typedef struct {
#ifdef JANET_WINDOWS
    SOCKET socket;
#else
    int fd;
#endif
    int flags;
} JanetStream;

static int janet_stream_close(void *p, size_t s);

static int janet_stream_getter(void *p, Janet key, Janet *out);

static const JanetAbstractType StreamAT = {
    "core/stream",
    janet_stream_close,
    NULL,
    janet_stream_getter,
    JANET_ATEND_GET
};

static int janet_stream_close(void *p, size_t s) {
    (void) s;
    JanetStream *stream = p;
    if (!(stream->flags & JANET_STREAM_CLOSED)) {
        stream->flags |= JANET_STREAM_CLOSED;
#ifdef JANET_WINDOWS
        closesocket(stream->socket);
#else
        close(stream->fd);
#endif
    }
    return 0;
}

#ifdef JANET_WINDOWS
static JanetStream *make_stream(SOCKET socket, int flags) {
    u_long iMode = 0;
    JanetStream *stream = janet_abstract(&StreamAT, sizeof(JanetStream));
    ioctlsocket(socket, FIONBIO, &iMode);
    stream->socket = socket;
    stream->flags = flags;
    return stream;
}
#else
static JanetStream *make_stream(int fd, int flags) {
    JanetStream *stream = janet_abstract(&StreamAT, sizeof(JanetStream));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    stream->fd = fd;
    stream->flags = flags;
    return stream;
}
#endif

/*
 * Event loop
 */

/* This large struct describes a waiting file descriptor, as well
 * as what to do when we get an event for it. It is a variant type, where
 * each variant implements a simple state machine. */
typedef struct {

    /* File descriptor to listen for events on. */
    JanetStream *stream;

    /* Fiber to resume when event finishes. Can be NULL, in which case,
     * no fiber is resumed when event completes. */
    JanetFiber *fiber;

    /* What kind of event we are listening for.
     * As more IO functionality get's added, we can
     * expand this. */
    enum {
        JLE_READ_CHUNK,
        JLE_READ_SOME,
        JLE_READ_ACCEPT,
        JLE_CONNECT,
        JLE_WRITE_FROM_BUFFER,
        JLE_WRITE_FROM_STRINGLIKE
    } event_type;

    /* Each variant can have a different payload. */
    union {

        /* JLE_READ_CHUNK/JLE_READ_SOME */
        struct {
            int32_t bytes_left;
            JanetBuffer *buf;
        } read_chunk;

        /* JLE_READ_ACCEPT */
        struct {
            JanetFunction *handler;
        } read_accept;

        /* JLE_WRITE_FROM_BUFFER */
        struct {
            JanetBuffer *buf;
            int32_t start;
        } write_from_buffer;

        /* JLE_WRITE_FROM_STRINGLIKE */
        struct {
            const uint8_t *str;
            int32_t start;
        } write_from_stringlike;

    } data;

} JanetLoopFD;

#define JANET_LOOPFD_MAX 1024

/* Global loop data */
#ifdef JANET_WINDOWS
JANET_THREAD_LOCAL WSAPOLLFD janet_vm_pollfds[JANET_LOOPFD_MAX];
#else
JANET_THREAD_LOCAL struct pollfd janet_vm_pollfds[JANET_LOOPFD_MAX];
#endif
JANET_THREAD_LOCAL JanetLoopFD janet_vm_loopfds[JANET_LOOPFD_MAX];
JANET_THREAD_LOCAL int janet_vm_loop_count;

/* We could also add/remove gc roots. This is easier for now. */
void janet_net_markloop(void) {
    for (int i = 0; i < janet_vm_loop_count; i++) {
        JanetLoopFD lfd = janet_vm_loopfds[i];
        if (lfd.fiber != NULL) {
            janet_mark(janet_wrap_fiber(lfd.fiber));
        }
        janet_mark(janet_wrap_abstract(lfd.stream));
        switch (lfd.event_type) {
            default:
                break;
            case JLE_READ_CHUNK:
            case JLE_READ_SOME:
                janet_mark(janet_wrap_buffer(lfd.data.read_chunk.buf));
                break;
            case JLE_READ_ACCEPT:
                janet_mark(janet_wrap_function(lfd.data.read_accept.handler));
                break;
            case JLE_CONNECT:
                break;
            case JLE_WRITE_FROM_BUFFER:
                janet_mark(janet_wrap_buffer(lfd.data.write_from_buffer.buf));
                break;
            case JLE_WRITE_FROM_STRINGLIKE:
                janet_mark(janet_wrap_string(lfd.data.write_from_stringlike.str));
        }
    }
}

/* Add a loop fd to the global event loop */
static int janet_loop_schedule(JanetLoopFD lfd, short events) {
    if (janet_vm_loop_count == JANET_LOOPFD_MAX) {
        return -1;
    }
    int index = janet_vm_loop_count++;
    janet_vm_loopfds[index] = lfd;
#ifdef JANET_WINDOWS
    janet_vm_pollfds[index].fd = lfd.stream->socket;
#else
    janet_vm_pollfds[index].fd = lfd.stream->fd;
#endif
    janet_vm_pollfds[index].events = events;
    janet_vm_pollfds[index].revents = 0;
    return index;
}

/* Remove event from list */
static void janet_loop_rmindex(int index) {
    janet_vm_loopfds[index] = janet_vm_loopfds[--janet_vm_loop_count];
    janet_vm_pollfds[index] = janet_vm_pollfds[janet_vm_loop_count];
}


/* Return delta in number of loop fds. Abstracted out so
 * we can separate out the polling logic */
static size_t janet_loop_event(size_t index) {
    JanetLoopFD *jlfd = janet_vm_loopfds + index;
    JanetStream *stream = jlfd->stream;
#ifdef JANET_WINDOWS
    SOCKET socket = stream->socket;
#else
    int fd = stream->fd;
#endif
    int ret = 1;
    int should_resume = 0;
    Janet resumeval = janet_wrap_nil();
    if (stream->flags & JANET_STREAM_CLOSED) {
        should_resume = 1;
        ret = 0;
    } else {
        switch (jlfd->event_type) {
            case JLE_READ_CHUNK:
            case JLE_READ_SOME: {
                JanetBuffer *buffer = jlfd->data.read_chunk.buf;
                int32_t bytes_left = jlfd->data.read_chunk.bytes_left;
                janet_buffer_extra(buffer, bytes_left);
                if (!(stream->flags & JANET_STREAM_READABLE)) {
                    should_resume = 1;
                    ret = 0;
                    break;
                }
#ifdef JANET_WINDOWS
                long nread;
                do {
                    nread = recv(socket, buffer->data + buffer->count, bytes_left, 0);
                } while (nread == -1 && WSAGetLastError() == WSAEINTR);
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    ret = 1;
                    break;
                }
#else
                ssize_t nread;
                do {
                    nread = read(fd, buffer->data + buffer->count, bytes_left);
                } while (nread == -1 && errno == EINTR);
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ret = 1;
                    break;
                }
#endif
                if (nread > 0) {
                    buffer->count += nread;
                    bytes_left -= nread;
                } else {
                    bytes_left = 0;
                }
                if (jlfd->event_type == JLE_READ_SOME || bytes_left == 0) {
                    should_resume = 1;
                    if (nread > 0) {
                        resumeval = janet_wrap_buffer(buffer);
                    }
                    ret = 0;
                } else {
                    jlfd->data.read_chunk.bytes_left = bytes_left;
                    ret = 1;
                }
                break;
            }
            case JLE_READ_ACCEPT: {
#ifdef JANET_WINDOWS
                SOCKET connfd = accept(socket, NULL, NULL);
                if (connfd != INVALID_SOCKET) {
#else
                char addr[256] = {0}; /* Just make sure it is large enough for largest address type */
                socklen_t len = 0;
                int connfd = accept(fd, (void *) &addr, &len);
                if (connfd >= 0) {
#endif
                    /* Made a new connection socket */
                    JanetStream *stream = make_stream(connfd, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
                    Janet streamv = janet_wrap_abstract(stream);
                    JanetFunction *handler = jlfd->data.read_accept.handler;
                    Janet out;
                    JanetFiber *fiberp = NULL;
                    /* Launch connection fiber */
                    JanetSignal sig = janet_pcall(handler, 1, &streamv, &out, &fiberp);
                    if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_EVENT) {
                        janet_stacktrace(fiberp, out);
                    }
                }
                ret = JANET_LOOPFD_MAX;
                break;
            }
            case JLE_WRITE_FROM_BUFFER:
            case JLE_WRITE_FROM_STRINGLIKE: {
                int32_t start, len;
                const uint8_t *bytes;
                if (!(stream->flags & JANET_STREAM_WRITABLE)) {
                    should_resume = 1;
                    ret = 0;
                    break;
                }
                if (jlfd->event_type == JLE_WRITE_FROM_BUFFER) {
                    JanetBuffer *buffer = jlfd->data.write_from_buffer.buf;
                    bytes = buffer->data;
                    len = buffer->count;
                    start = jlfd->data.write_from_buffer.start;
                } else {
                    bytes = jlfd->data.write_from_stringlike.str;
                    len = janet_string_length(bytes);
                    start = jlfd->data.write_from_stringlike.start;
                }
                if (start < len) {
                    int32_t nbytes = len - start;
#ifdef JANET_WINDOWS
                    long nwrote;
                    do {
                        nwrote = send(socket, bytes + start, nbytes, 0);
                    } while (nwrote == -1 && WSAGetLastError() == WSAEINTR);
#else
                    ssize_t nwrote;
                    do {
                        nwrote = write(fd, bytes + start, nbytes);
                    } while (nwrote == -1 && errno == EINTR);
#endif
                    if (nwrote > 0) {
                        start += nwrote;
                    } else {
                        start = len;
                    }
                }
                if (start >= len) {
                    should_resume = 1;
                    ret = 0;
                } else {
                    if (jlfd->event_type == JLE_WRITE_FROM_BUFFER) {
                        jlfd->data.write_from_buffer.start = start;
                    } else {
                        jlfd->data.write_from_stringlike.start = start;
                    }
                    ret = 1;
                }
                break;
            }
            case JLE_CONNECT: {
                break;
            }
    }
    }

    /* Resume a fiber for some events */
    if (NULL != jlfd->fiber && should_resume) {
        /* Resume the fiber */
        Janet out;
        JanetSignal sig = janet_continue(jlfd->fiber, resumeval, &out);
        if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_EVENT) {
            janet_stacktrace(jlfd->fiber, out);
        }
    }

    /* Remove this handler from the handler pool. */
    if (should_resume) janet_loop_rmindex((int) index);

    return ret;
}

static void janet_loop1(void) {
    /* Remove closed file descriptors */
    for (int i = 0; i < janet_vm_loop_count;) {
        if (janet_vm_loopfds[i].stream->flags & JANET_STREAM_CLOSED) {
            janet_loop_rmindex(i);
        } else {
            i++;
        }
    }
    /* Poll */
    if (janet_vm_loop_count == 0) return;
    int ready;
#ifdef JANET_WINDOWS
    do {
        ready = WSAPoll(janet_vm_pollfds, janet_vm_loop_count, -1);
    } while (ready == -1 && WSAGetLastError() == WSAEINTR);
    if (ready == -1) return;
#else
    do {
        ready = poll(janet_vm_pollfds, janet_vm_loop_count, -1);
    } while (ready == -1 && errno == EAGAIN);
    if (ready == -1) return;
#endif
    /* Handle events */
    for (int i = 0; i < janet_vm_loop_count;) {
        int revents = janet_vm_pollfds[i].revents;
        janet_vm_pollfds[i].revents = 0;
        if ((janet_vm_pollfds[i].events | POLLHUP | POLLERR) & revents) {
            size_t delta = janet_loop_event(i);
            i += (int) delta;
        } else {
            i++;
        }
    }
}

void janet_loop(void) {
    while (janet_vm_loop_count) {
        janet_loop1();
    }
}

/*
 * Scheduling Helpers
 */

#define JANET_SCHED_FSOME 1

JANET_NO_RETURN static void janet_sched_read(JanetStream *stream, JanetBuffer *buf, int32_t nbytes, int flags) {
    JanetLoopFD lfd = {0};
    lfd.stream = stream;
    lfd.fiber = janet_root_fiber();
    lfd.event_type = (flags & JANET_SCHED_FSOME) ? JLE_READ_SOME : JLE_READ_CHUNK;
    lfd.data.read_chunk.buf = buf;
    lfd.data.read_chunk.bytes_left = nbytes;
    janet_loop_schedule(lfd, POLLIN);
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

JANET_NO_RETURN static void janet_sched_write_buffer(JanetStream *stream, JanetBuffer *buf) {
    JanetLoopFD lfd = {0};
    lfd.stream = stream;
    lfd.fiber = janet_root_fiber();
    lfd.event_type = JLE_WRITE_FROM_BUFFER;
    lfd.data.write_from_buffer.buf = buf;
    lfd.data.write_from_buffer.start = 0;
    janet_loop_schedule(lfd, POLLOUT);
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

JANET_NO_RETURN static void janet_sched_write_stringlike(JanetStream *stream, const uint8_t *str) {
    JanetLoopFD lfd = {0};
    lfd.stream = stream;
    lfd.fiber = janet_root_fiber();
    lfd.event_type = JLE_WRITE_FROM_STRINGLIKE;
    lfd.data.write_from_stringlike.str = str;
    lfd.data.write_from_stringlike.start = 0;
    janet_loop_schedule(lfd, POLLOUT);
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

/* Needs argc >= offset + 2 */
static struct addrinfo *janet_get_addrinfo(Janet *argv, int32_t offset) {
    /* Get host and port */
    const char *host = janet_getcstring(argv, offset);
    const char *port = janet_getcstring(argv, offset + 1);
    /* getaddrinfo */
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_PASSIVE;
    int status = getaddrinfo(host, port, &hints, &ai);
    if (status) {
        janet_panicf("could not get address info: %s", gai_strerror(status));
    }
    return ai;
}

/*
 * C Funs
 */

static Janet cfun_net_connect(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);

    struct addrinfo *ai = janet_get_addrinfo(argv, 0);

#ifdef JANET_WINDOWS
    /* Create socket */
    SOCKET sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(ai);
        janet_panic("could not create socket");
    }

    /* Connect to socket */
    int status = connect(sock, ai->ai_addr, (int) ai->ai_addrlen);
    freeaddrinfo(ai);
    if (status == -1) {
        closesocket(sock);
        janet_panic("could not connect to socket");
    }
#else
    /* Create socket */
    int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(ai);
        janet_panic("could not create socket");
    }

    /* Connect to socket */
    int status = connect(sock, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);
    if (status < 0) {
        close(sock);
        janet_panic("could not connect to socket");
    }
#endif

    /* Wrap socket in abstract type JanetStream */
    JanetStream *stream = make_stream(sock, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
    return janet_wrap_abstract(stream);
}

static Janet cfun_net_server(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);

    /* Get host, port, and handler*/
    JanetFunction *fun = janet_getfunction(argv, 2);

    struct addrinfo *ai = janet_get_addrinfo(argv, 0);

#ifdef JANET_WINDOWS
    /* Check all addrinfos in a loop for the first that we can bind to. */
    SOCKET sfd = INVALID_SOCKET;
    struct addrinfo *rp = NULL;
    for (rp = ai; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == INVALID_SOCKET) continue;
        /* Set various socket options */
        int enable = 1;
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable, sizeof(int)) < 0) {
            closesocket(sfd);
            janet_panic("setsockopt(SO_REUSEADDR) failed");
        }
        /* Bind */
        if (bind(sfd, rp->ai_addr, (int) rp->ai_addrlen) == 0) break;
        closesocket(sfd);
    }
    if (NULL == rp) {
        freeaddrinfo(ai);
        janet_panic("could not bind to any sockets");
    }

    /* listen */
    int status = listen(sfd, 1024);
    freeaddrinfo(ai);
    if (status) {
        closesocket(sfd);
        janet_panic("could not listen on file descriptor");
    }
#else
    /* Check all addrinfos in a loop for the first that we can bind to. */
    int sfd = 0;
    struct addrinfo *rp = NULL;
    for (rp = ai; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
        /* Set various socket options */
        int enable = 1;
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            close(sfd);
            janet_panic("setsockopt(SO_REUSEADDR) failed");
        }
#ifdef SO_REUSEPORT
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
            close(sfd);
            janet_panic("setsockopt(SO_REUSEPORT) failed");
        }
#endif
        /* Bind */
        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sfd);
    }
    if (NULL == rp) {
        freeaddrinfo(ai);
        janet_panic("could not bind to any sockets");
    }

    /* listen */
    int status = listen(sfd, 1024);
    freeaddrinfo(ai);
    if (status) {
        close(sfd);
        janet_panic("could not listen on file descriptor");
    }

    /* We need to ignore sigpipe when reading and writing to our connection socket.
     * Since a connection could be disconnected at any time, any read or write may fail.
     * We don't want to blow up the whole application. */
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Put sfd on our loop */
    JanetLoopFD lfd = {0};
    lfd.stream = make_stream(sfd, 0);
    lfd.event_type = JLE_READ_ACCEPT;
    lfd.data.read_accept.handler = fun;
    janet_loop_schedule(lfd, POLLIN);

    return janet_wrap_abstract(lfd.stream);
}

static Janet cfun_stream_read(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    janet_sched_read(stream, buffer, n, JANET_SCHED_FSOME);
}

static Janet cfun_stream_chunk(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &StreamAT);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    janet_sched_read(stream, buffer, n, 0);
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
    if (janet_checktype(argv[1], JANET_BUFFER)) {
        janet_sched_write_buffer(stream, janet_getbuffer(argv, 1));
    } else {
        JanetByteView bytes = janet_getbytes(argv, 1);
        janet_sched_write_stringlike(stream, bytes.bytes);
    }
}

static const JanetMethod stream_methods[] = {
    {"chunk", cfun_stream_chunk},
    {"close", cfun_stream_close},
    {"read", cfun_stream_read},
    {"write", cfun_stream_write},
    {NULL, NULL}
};

static int janet_stream_getter(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), stream_methods, out);
}

static const JanetReg net_cfuns[] = {
    {
        "net/server", cfun_net_server,
        JDOC("(net/server host port)\n\nStart a TCP server.")
    },
    {"net/read", cfun_stream_read, NULL},
    {"net/chunk", cfun_stream_chunk, NULL},
    {"net/write", cfun_stream_write, NULL},
    {"net/close", cfun_stream_close, NULL},
    {"net/connect", cfun_net_connect, NULL},
    {NULL, NULL, NULL}
};

void janet_lib_net(JanetTable *env) {
    janet_vm_loop_count = 0;
#ifdef JANET_WINDOWS
    WSADATA wsaData;
    janet_assert(!WSAStartup(MAKEWORD(2, 2), &wsaData), "could not start winsock");
#endif
    janet_core_cfuns(env, NULL, net_cfuns);
}

void janet_net_deinit(void) {
#ifdef JANET_WINDOWS
    WSACleanup();
#endif
}
