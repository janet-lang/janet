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

#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>

/*
 * Streams
 */

#define JANET_STREAM_CLOSED 1
#define JANET_STREAM_READABLE 2
#define JANET_STREAM_WRITABLE 4

typedef struct {
    int fd;
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
        close(stream->fd);
    }
    return 0;
}

static JanetStream *make_stream(int fd, int flags) {
    JanetStream *stream = janet_abstract(&StreamAT, sizeof(JanetStream));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    stream->fd = fd;
    stream->flags = flags;
    return stream;
}

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
static int janet_loop_schedule(JanetLoopFD lfd) {
    if (janet_vm_loop_count == JANET_LOOPFD_MAX) {
        return -1;
    }
    int index = janet_vm_loop_count;
    janet_vm_loopfds[janet_vm_loop_count++] = lfd;
    return index;
}

/* Return delta in number of loop fds. Abstracted out so
 * we can separate out the polling logic */
static size_t janet_loop_event(size_t index) {
    JanetLoopFD *jlfd = janet_vm_loopfds + index;
    JanetStream *stream = jlfd->stream;
    int fd = stream->fd;
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
                ssize_t nread;
                errno = 0;
                if (!(stream->flags & JANET_STREAM_READABLE)) {
                    should_resume = 1;
                    ret = 0;
                    break;
                }
                do {
                    nread = read(fd, buffer->data + buffer->count, bytes_left);
                } while (errno == EINTR);
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ret = 1;
                    break;
                }
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
                char addr[256] = {0}; /* Just make sure it is large enough for largest address type */
                socklen_t len = 0;
                int connfd = accept(fd, (void *) &addr, &len);
                if (connfd >= 0) {
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
                    ssize_t nwrote;
                    errno = 0;
                    do {
                        nwrote = write(fd, bytes + start, nbytes);
                    } while (errno == EINTR);
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
    if (should_resume) {
        janet_vm_loopfds[index] = janet_vm_loopfds[--janet_vm_loop_count];
    }

    return ret;
}

static void janet_loop1(void) {
    /* Set up fd_sets */
    fd_set readfds;
    fd_set writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    int fd_max = 0;
    for (int i = 0; i < janet_vm_loop_count; i++) {
        JanetLoopFD *jlfd = janet_vm_loopfds + i;
        int fd = jlfd->stream->fd;
        if (fd > fd_max) fd_max = fd;
        fd_set *set = (jlfd->event_type <= JLE_READ_ACCEPT) ? &readfds : &writefds;
        FD_SET(fd, set);
    }

    /* Blocking call - we should add timeout functionality */
    select(fd_max + 1, &readfds, &writefds, NULL, NULL);

    /* Now handle all events */
    for (int i = 0; i < janet_vm_loop_count;) {
        JanetLoopFD *jlfd = janet_vm_loopfds + i;
        int fd = jlfd->stream->fd;
        fd_set *set = (jlfd->event_type <= JLE_READ_ACCEPT) ? &readfds : &writefds;
        if (FD_ISSET(fd, set)) {
            size_t delta = janet_loop_event(i);
            i += delta;
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
    janet_loop_schedule(lfd);
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

JANET_NO_RETURN static void janet_sched_write_buffer(JanetStream *stream, JanetBuffer *buf) {
    JanetLoopFD lfd = {0};
    lfd.stream = stream;
    lfd.fiber = janet_root_fiber();
    lfd.event_type = JLE_WRITE_FROM_BUFFER;
    lfd.data.write_from_buffer.buf = buf;
    lfd.data.write_from_buffer.start = 0;
    janet_loop_schedule(lfd);
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

JANET_NO_RETURN static void janet_sched_write_stringlike(JanetStream *stream, const uint8_t *str) {
    JanetLoopFD lfd = {0};
    lfd.stream = stream;
    lfd.fiber = janet_root_fiber();
    lfd.event_type = JLE_WRITE_FROM_STRINGLIKE;
    lfd.data.write_from_stringlike.str = str;
    lfd.data.write_from_stringlike.start = 0;
    janet_loop_schedule(lfd);
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

    /* Wrap socket in abstract type JanetStream */
    JanetStream *stream = make_stream(sock, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
    return janet_wrap_abstract(stream);
}

static Janet cfun_net_server(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);

    /* Get host, port, and handler*/
    JanetFunction *fun = janet_getfunction(argv, 2);

    struct addrinfo *ai = janet_get_addrinfo(argv, 0);

    /* Check all addrinfos in a loop for the first that we can bind to. */
    int sfd = 0;
    struct addrinfo *rp = NULL;
    for (rp = ai; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
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

    /* Put sfd on our loop */
    JanetLoopFD lfd = {0};
    lfd.stream = make_stream(sfd, 0);
    lfd.event_type = JLE_READ_ACCEPT;
    lfd.data.read_accept.handler = fun;
    janet_loop_schedule(lfd);

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
    janet_core_cfuns(env, NULL, net_cfuns);
}

