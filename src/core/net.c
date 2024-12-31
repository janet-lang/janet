/*
* Copyright (c) 2024 Calvin Rose and contributors.
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
#include "fiber.h"
#endif

#ifdef JANET_NET

#include <math.h>
#ifdef JANET_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#ifdef JANET_MSVC
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "Advapi32.lib")
#endif
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#endif

const JanetAbstractType janet_address_type = {
    "core/socket-address",
    JANET_ATEND_NAME
};

#ifdef JANET_WINDOWS
#define JSOCKCLOSE(x) closesocket((SOCKET) x)
#define JSOCKDEFAULT INVALID_SOCKET
#define JSOCKVALID(x) ((x) != INVALID_SOCKET)
#define JSock SOCKET
#define JSOCKFLAGS 0
#else
#define JSOCKCLOSE(x) close(x)
#define JSOCKDEFAULT 0
#define JSOCKVALID(x) ((x) >= 0)
#define JSock int
#ifdef SOCK_CLOEXEC
#define JSOCKFLAGS SOCK_CLOEXEC
#else
#define JSOCKFLAGS 0
#endif
#endif

/* maximum number of bytes in a socket address host (post name resolution) */
#ifdef JANET_WINDOWS
#ifdef JANET_NO_IPV6
#define SA_ADDRSTRLEN (INET_ADDRSTRLEN + 1)
#else
#define SA_ADDRSTRLEN (INET6_ADDRSTRLEN + 1)
#endif
typedef unsigned short in_port_t;
#else
#define JANET_SA_MAX(a, b) (((a) > (b))? (a) : (b))
#ifdef JANET_NO_IPV6
#define SA_ADDRSTRLEN JANET_SA_MAX(INET_ADDRSTRLEN + 1, (sizeof ((struct sockaddr_un *)0)->sun_path) + 1)
#else
#define SA_ADDRSTRLEN JANET_SA_MAX(INET6_ADDRSTRLEN + 1, (sizeof ((struct sockaddr_un *)0)->sun_path) + 1)
#endif
#endif

static JanetStream *make_stream(JSock handle, uint32_t flags);

/* We pass this flag to all send calls to prevent sigpipe */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Make sure a socket doesn't block */
static void janet_net_socknoblock(JSock s) {
#ifdef JANET_WINDOWS
    unsigned long arg = 1;
    ioctlsocket(s, FIONBIO, &arg);
#else
#if !defined(SOCK_CLOEXEC) && defined(O_CLOEXEC)
    int extra = O_CLOEXEC;
#else
    int extra = 0;
#endif
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK | extra);
#ifdef SO_NOSIGPIPE
    int enable = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(int));
#endif
#endif
}

/* State machine for async connect */

void net_callback_connect(JanetFiber *fiber, JanetAsyncEvent event) {
    JanetStream *stream = fiber->ev_stream;
    switch (event) {
        default:
            break;
#ifndef JANET_WINDOWS
        /* Wait until we have an actual event before checking.
         * Windows doesn't support async connect with this, just try immediately.*/
        case JANET_ASYNC_EVENT_INIT:
#endif
        case JANET_ASYNC_EVENT_DEINIT:
            return;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_cancel(fiber, janet_cstringv("stream closed"));
            janet_async_end(fiber);
            return;
    }
#ifdef JANET_WINDOWS
    int res = 0;
    int size = sizeof(res);
    int r = getsockopt((SOCKET)stream->handle, SOL_SOCKET, SO_ERROR, (char *)&res, &size);
#else
    int res = 0;
    socklen_t size = sizeof res;
    int r = getsockopt(stream->handle, SOL_SOCKET, SO_ERROR, &res, &size);
#endif
    if (r == 0) {
        if (res == 0) {
            janet_schedule(fiber, janet_wrap_abstract(stream));
        } else {
            janet_cancel(fiber, janet_cstringv(janet_strerror(res)));
            stream->flags |= JANET_STREAM_TOCLOSE;
        }
    } else {
        janet_cancel(fiber, janet_ev_lasterr());
        stream->flags |= JANET_STREAM_TOCLOSE;
    }
    janet_async_end(fiber);
}

static JANET_NO_RETURN void net_sched_connect(JanetStream *stream) {
    janet_async_start(stream, JANET_ASYNC_LISTEN_WRITE, net_callback_connect, NULL);
}

/* State machine for accepting connections. */

#ifdef JANET_WINDOWS

typedef struct {
    WSAOVERLAPPED overlapped;
    JanetFunction *function;
    JanetStream *lstream;
    JanetStream *astream;
    char buf[1024];
} NetStateAccept;

static int net_sched_accept_impl(NetStateAccept *state, JanetFiber *fiber, Janet *err);

void net_callback_accept(JanetFiber *fiber, JanetAsyncEvent event) {
    NetStateAccept *state = (NetStateAccept *)fiber->ev_state;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK: {
            if (state->lstream) janet_mark(janet_wrap_abstract(state->lstream));
            if (state->astream) janet_mark(janet_wrap_abstract(state->astream));
            if (state->function) janet_mark(janet_wrap_function(state->function));
            break;
        }
        case JANET_ASYNC_EVENT_CLOSE:
            janet_schedule(fiber, janet_wrap_nil());
            janet_async_end(fiber);
            return;
        case JANET_ASYNC_EVENT_COMPLETE: {
            if (state->astream->flags & JANET_STREAM_CLOSED) {
                janet_cancel(fiber, janet_cstringv("failed to accept connection"));
                janet_async_end(fiber);
                return;
            }
            SOCKET lsock = (SOCKET) state->lstream->handle;
            if (NO_ERROR != setsockopt((SOCKET) state->astream->handle, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                                       (char *) &lsock, sizeof(lsock))) {
                janet_cancel(fiber, janet_cstringv("failed to accept connection"));
                janet_async_end(fiber);
                return;
            }

            Janet streamv = janet_wrap_abstract(state->astream);
            if (state->function) {
                /* Schedule worker */
                JanetFiber *sub_fiber = janet_fiber(state->function, 64, 1, &streamv);
                sub_fiber->supervisor_channel = fiber->supervisor_channel;
                janet_schedule(sub_fiber, janet_wrap_nil());
                /* Now listen again for next connection */
                Janet err;
                if (net_sched_accept_impl(state, fiber, &err)) {
                    janet_cancel(fiber, err);
                    janet_async_end(fiber);
                    return;
                }
            } else {
                janet_schedule(fiber, streamv);
                janet_async_end(fiber);
                return;
            }
        }
    }
}

JANET_NO_RETURN static void janet_sched_accept(JanetStream *stream, JanetFunction *fun) {
    Janet err;
    NetStateAccept *state = janet_malloc(sizeof(NetStateAccept));
    memset(&state->overlapped, 0, sizeof(WSAOVERLAPPED));
    memset(&state->buf, 0, 1024);
    state->function = fun;
    state->lstream = stream;
    if (net_sched_accept_impl(state, janet_root_fiber(), &err)) {
        janet_free(state);
        janet_panicv(err);
    }
    janet_async_start(stream, JANET_ASYNC_LISTEN_READ, net_callback_accept, state);
}

static int net_sched_accept_impl(NetStateAccept *state, JanetFiber *fiber, Janet *err) {
    SOCKET lsock = (SOCKET) state->lstream->handle;
    SOCKET asock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (asock == INVALID_SOCKET) {
        *err = janet_ev_lasterr();
        return 1;
    }
    JanetStream *astream = make_stream(asock, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
    state->astream = astream;
    int socksize = sizeof(SOCKADDR_STORAGE) + 16;
    if (FALSE == AcceptEx(lsock, asock, state->buf, 0, socksize, socksize, NULL, &state->overlapped)) {
        int code = WSAGetLastError();
        if (code == WSA_IO_PENDING) {
            /* indicates io is happening async */
            janet_async_in_flight(fiber);
            return 0;
        }
        *err = janet_ev_lasterr();
        return 1;
    }
    return 0;
}

#else

typedef struct {
    JanetFunction *function;
} NetStateAccept;

void net_callback_accept(JanetFiber *fiber, JanetAsyncEvent event) {
    JanetStream *stream = fiber->ev_stream;
    NetStateAccept *state = (NetStateAccept *)fiber->ev_state;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK: {
            if (state->function) janet_mark(janet_wrap_function(state->function));
            break;
        }
        case JANET_ASYNC_EVENT_CLOSE:
            janet_schedule(fiber, janet_wrap_nil());
            janet_async_end(fiber);
            return;
        case JANET_ASYNC_EVENT_INIT:
        case JANET_ASYNC_EVENT_READ: {
#if defined(JANET_LINUX)
            JSock connfd = accept4(stream->handle, NULL, NULL, SOCK_CLOEXEC);
#else
            /* On BSDs, CLOEXEC should be inherited from server socket */
            JSock connfd = accept(stream->handle, NULL, NULL);
#endif
            if (JSOCKVALID(connfd)) {
                janet_net_socknoblock(connfd);
                JanetStream *stream = make_stream(connfd, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);
                Janet streamv = janet_wrap_abstract(stream);
                if (state->function) {
                    JanetFiber *sub_fiber = janet_fiber(state->function, 64, 1, &streamv);
                    sub_fiber->supervisor_channel = fiber->supervisor_channel;
                    janet_schedule(sub_fiber, janet_wrap_nil());
                } else {
                    janet_schedule(fiber, streamv);
                    janet_async_end(fiber);
                    return;
                }
            }
            break;
        }
    }
}

JANET_NO_RETURN static void janet_sched_accept(JanetStream *stream, JanetFunction *fun) {
    NetStateAccept *state = janet_malloc(sizeof(NetStateAccept));
    memset(state, 0, sizeof(NetStateAccept));
    state->function = fun;
    if (fun) janet_stream_level_triggered(stream);
    janet_async_start(stream, JANET_ASYNC_LISTEN_READ, net_callback_accept, state);
}

#endif

/* Address info */

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
/* For unix paths, just rertuns a single sockaddr and sets *is_unix to 1,
 * otherwise 0. Also, ignores is_bind when is a unix socket. */
static struct addrinfo *janet_get_addrinfo(Janet *argv, int32_t offset, int socktype, int passive, int *is_unix) {
    /* Unix socket support - not yet supported on windows. */
#ifndef JANET_WINDOWS
    if (janet_keyeq(argv[offset], "unix")) {
        const char *path = janet_getcstring(argv, offset + 1);
        struct sockaddr_un *saddr = janet_calloc(1, sizeof(struct sockaddr_un));
        if (saddr == NULL) {
            JANET_OUT_OF_MEMORY;
        }
        saddr->sun_family = AF_UNIX;
        size_t path_size = sizeof(saddr->sun_path);
#ifdef JANET_LINUX
        if (path[0] == '@') {
            saddr->sun_path[0] = '\0';
            snprintf(saddr->sun_path + 1, path_size - 1, "%s", path + 1);
        } else
#endif
        {
            snprintf(saddr->sun_path, path_size, "%s", path);
        }
        *is_unix = 1;
        return (struct addrinfo *) saddr;
    }
#endif
    /* Get host and port */
    char *host = (char *)janet_getcstring(argv, offset);
    char *port = NULL;
    if (janet_checkint(argv[offset + 1])) {
        port = (char *)janet_to_string(argv[offset + 1]);
    } else {
        port = (char *)janet_optcstring(argv, offset + 2, offset + 1, NULL);
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

JANET_CORE_FN(cfun_net_sockaddr,
              "(net/address host port &opt type multi)",
              "Look up the connection information for a given hostname, port, and connection type. Returns "
              "a handle that can be used to send datagrams over network without establishing a connection. "
              "On Posix platforms, you can use :unix for host to connect to a unix domain socket, where the name is "
              "given in the port argument. On Linux, abstract "
              "unix domain sockets are specified with a leading '@' character in port. If `multi` is truthy, will "
              "return all address that match in an array instead of just the first.") {
    janet_sandbox_assert(JANET_SANDBOX_NET_CONNECT); /* connect OR listen */
    janet_arity(argc, 2, 4);
    int socktype = janet_get_sockettype(argv, argc, 2);
    int is_unix = 0;
    int make_arr = (argc >= 3 && janet_truthy(argv[3]));
    struct addrinfo *ai = janet_get_addrinfo(argv, 0, socktype, 0, &is_unix);
#ifndef JANET_WINDOWS
    /* no unix domain socket support on windows yet */
    if (is_unix) {
        void *abst = janet_abstract(&janet_address_type, sizeof(struct sockaddr_un));
        memcpy(abst, ai, sizeof(struct sockaddr_un));
        Janet ret = janet_wrap_abstract(abst);
        return make_arr ? janet_wrap_array(janet_array_n(&ret, 1)) : ret;
    }
#endif
    if (make_arr) {
        /* Select all */
        JanetArray *arr = janet_array(10);
        struct addrinfo *iter = ai;
        while (NULL != iter) {
            void *abst = janet_abstract(&janet_address_type, iter->ai_addrlen);
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
        void *abst = janet_abstract(&janet_address_type, ai->ai_addrlen);
        memcpy(abst, ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(ai);
        return janet_wrap_abstract(abst);
    }
}

JANET_CORE_FN(cfun_net_connect,
              "(net/connect host port &opt type bindhost bindport)",
              "Open a connection to communicate with a server. Returns a duplex stream "
              "that can be used to communicate with the server. Type is an optional keyword "
              "to specify a connection type, either :stream or :datagram. The default is :stream. "
              "Bindhost is an optional string to select from what address to make the outgoing "
              "connection, with the default being the same as using the OS's preferred address. ") {
    janet_sandbox_assert(JANET_SANDBOX_NET_CONNECT);
    janet_arity(argc, 2, 5);

    /* Check arguments */
    int socktype = janet_get_sockettype(argv, argc, 2);
    int is_unix = 0;
    char *bindhost = (char *) janet_optcstring(argv, argc, 3, NULL);
    char *bindport = NULL;
    if (argc >= 5 && janet_checkint(argv[4])) {
        bindport = (char *)janet_to_string(argv[4]);
    } else {
        bindport = (char *)janet_optcstring(argv, argc, 4, NULL);
    }

    /* Where we're connecting to */
    struct addrinfo *ai = janet_get_addrinfo(argv, 0, socktype, 0, &is_unix);

    /* Check if we're binding address */
    struct addrinfo *binding = NULL;
    if (bindhost != NULL) {
        if (is_unix) {
            freeaddrinfo(ai);
            janet_panic("bindhost not supported for unix domain sockets");
        }
        /* getaddrinfo */
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = socktype;
        hints.ai_flags = 0;
        int status = getaddrinfo(bindhost, bindport, &hints, &binding);
        if (status) {
            freeaddrinfo(ai);
            janet_panicf("could not get address info for bindhost: %s", gai_strerror(status));
        }
    }

    /* Create socket */
    JSock sock = JSOCKDEFAULT;
    void *addr = NULL;
    socklen_t addrlen = 0;
#ifndef JANET_WINDOWS
    if (is_unix) {
        sock = socket(AF_UNIX, socktype | JSOCKFLAGS, 0);
        if (!JSOCKVALID(sock)) {
            Janet v = janet_ev_lasterr();
            janet_free(ai);
            janet_panicf("could not create socket: %V", v);
        }
        addr = (void *) ai;
        addrlen = sizeof(struct sockaddr_un);
    } else
#endif
    {
        struct addrinfo *rp = NULL;
        for (rp = ai; rp != NULL; rp = rp->ai_next) {
#ifdef JANET_WINDOWS
            sock = WSASocketW(rp->ai_family, rp->ai_socktype, rp->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
            sock = socket(rp->ai_family, rp->ai_socktype | JSOCKFLAGS, rp->ai_protocol);
#endif
            if (JSOCKVALID(sock)) {
                addr = rp->ai_addr;
                addrlen = (socklen_t) rp->ai_addrlen;
                break;
            }
        }
        if (NULL == addr) {
            Janet v = janet_ev_lasterr();
            if (binding) freeaddrinfo(binding);
            freeaddrinfo(ai);
            janet_panicf("could not create socket: %V", v);
        }
    }

    /* Bind to bindhost and bindport if given */
    if (binding) {
        struct addrinfo *rp = NULL;
        int did_bind = 0;
        for (rp = binding; rp != NULL; rp = rp->ai_next) {
            if (bind(sock, rp->ai_addr, (int) rp->ai_addrlen) == 0) {
                did_bind = 1;
                break;
            }
        }
        if (!did_bind) {
            Janet v = janet_ev_lasterr();
            freeaddrinfo(binding);
            freeaddrinfo(ai);
            JSOCKCLOSE(sock);
            janet_panicf("could not bind outgoing address: %V", v);
        } else {
            freeaddrinfo(binding);
        }
    }

    /* Wrap socket in abstract type JanetStream */
    JanetStream *stream = make_stream(sock, JANET_STREAM_READABLE | JANET_STREAM_WRITABLE);

    /* Set up the socket for non-blocking IO before connecting */
    janet_net_socknoblock(sock);

    /* Connect to socket */
#ifdef JANET_WINDOWS
    int status = WSAConnect(sock, addr, addrlen, NULL, NULL, NULL, NULL);
    int err = WSAGetLastError();
    freeaddrinfo(ai);
#else
    int status = connect(sock, addr, addrlen);
    int err = errno;
    if (is_unix) {
        janet_free(ai);
    } else {
        freeaddrinfo(ai);
    }
#endif

    if (status) {
#ifdef JANET_WINDOWS
        if (err != WSAEWOULDBLOCK) {
#else
        if (err != EINPROGRESS) {
#endif
            JSOCKCLOSE(sock);
            Janet lasterr = janet_ev_lasterr();
            janet_panicf("could not connect socket: %V", lasterr);
        }
    }

    net_sched_connect(stream);
}

static const char *serverify_socket(JSock sfd, int reuse) {
    /* Set various socket options */
    int enable = 1;
    if (reuse) {
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable, sizeof(int)) < 0) {
            return "setsockopt(SO_REUSEADDR) failed";
        }
#ifdef SO_REUSEPORT
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
            return "setsockopt(SO_REUSEPORT) failed";
        }
#endif
    }
    janet_net_socknoblock(sfd);
    return NULL;
}

#ifdef JANET_WINDOWS
#define JANET_SHUTDOWN_RW SD_BOTH
#define JANET_SHUTDOWN_R SD_RECEIVE
#define JANET_SHUTDOWN_W SD_SEND
#else
#define JANET_SHUTDOWN_RW SHUT_RDWR
#define JANET_SHUTDOWN_R SHUT_RD
#define JANET_SHUTDOWN_W SHUT_WR
#endif

JANET_CORE_FN(cfun_net_shutdown,
              "(net/shutdown stream &opt mode)",
              "Stop communication on this socket in a graceful manner, either in both directions or just "
              "reading/writing from the stream. The `mode` parameter controls which communication to stop on the socket. "
              "\n\n* `:wr` is the default and prevents both reading new data from the socket and writing new data to the socket.\n"
              "* `:r` disables reading new data from the socket.\n"
              "* `:w` disable writing data to the socket.\n\n"
              "Returns the original socket.") {
    janet_arity(argc, 1, 2);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_SOCKET);
    int shutdown_type = JANET_SHUTDOWN_RW;
    if (argc == 2) {
        const uint8_t *kw = janet_getkeyword(argv, 1);
        if (0 == janet_cstrcmp(kw, "rw")) {
            shutdown_type = JANET_SHUTDOWN_RW;
        } else if (0 == janet_cstrcmp(kw, "r")) {
            shutdown_type = JANET_SHUTDOWN_R;
        } else if (0 == janet_cstrcmp(kw, "w")) {
            shutdown_type = JANET_SHUTDOWN_W;
        } else {
            janet_panicf("unexpected keyword %v", argv[1]);
        }
    }
    int status;
#ifdef JANET_WINDOWS
    status = shutdown((SOCKET) stream->handle, shutdown_type);
#else
    do {
        status = shutdown(stream->handle, shutdown_type);
    } while (status == -1 && errno == EINTR);
#endif
    if (status) {
        janet_panicf("could not shutdown socket: %V", janet_ev_lasterr());
    }
    return argv[0];
}

JANET_CORE_FN(cfun_net_listen,
              "(net/listen host port &opt type no-reuse)",
              "Creates a server. Returns a new stream that is neither readable nor "
              "writeable. Use net/accept or net/accept-loop be to handle connections and start the server. "
              "The type parameter specifies the type of network connection, either "
              "a :stream (usually tcp), or :datagram (usually udp). If not specified, the default is "
              ":stream. The host and port arguments are the same as in net/address. The last boolean parameter `no-reuse` will "
              "disable the use of SO_REUSEADDR and SO_REUSEPORT when creating a server on some operating systems.") {
    janet_sandbox_assert(JANET_SANDBOX_NET_LISTEN);
    janet_arity(argc, 2, 4);

    /* Get host, port, and handler*/
    int socktype = janet_get_sockettype(argv, argc, 2);
    int is_unix = 0;
    struct addrinfo *ai = janet_get_addrinfo(argv, 0, socktype, 1, &is_unix);
    int reuse = !(argc >= 4 && janet_truthy(argv[3]));

    JSock sfd = JSOCKDEFAULT;
#ifndef JANET_WINDOWS
    if (is_unix) {
        sfd = socket(AF_UNIX, socktype | JSOCKFLAGS, 0);
        if (!JSOCKVALID(sfd)) {
            janet_free(ai);
            janet_panicf("could not create socket: %V", janet_ev_lasterr());
        }
        const char *err = serverify_socket(sfd, reuse);
        if (NULL != err || bind(sfd, (struct sockaddr *)ai, sizeof(struct sockaddr_un))) {
            JSOCKCLOSE(sfd);
            janet_free(ai);
            if (err) {
                janet_panic(err);
            } else {
                janet_panicf("could not bind socket: %V", janet_ev_lasterr());
            }
        }
        janet_free(ai);
    } else
#endif
    {
        /* Check all addrinfos in a loop for the first that we can bind to. */
        struct addrinfo *rp = NULL;
        for (rp = ai; rp != NULL; rp = rp->ai_next) {
#ifdef JANET_WINDOWS
            sfd = WSASocketW(rp->ai_family, rp->ai_socktype | JSOCKFLAGS, rp->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
            sfd = socket(rp->ai_family, rp->ai_socktype | JSOCKFLAGS, rp->ai_protocol);
#endif
            if (!JSOCKVALID(sfd)) continue;
            const char *err = serverify_socket(sfd, reuse);
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
        JanetStream *stream = make_stream(sfd, JANET_STREAM_UDPSERVER | JANET_STREAM_READABLE);
        return janet_wrap_abstract(stream);
    } else {
        /* Stream server (TCP) */

        /* listen */
        int status = listen(sfd, 1024);
        if (status) {
            JSOCKCLOSE(sfd);
            janet_panicf("could not listen on file descriptor: %V", janet_ev_lasterr());
        }

        /* Put sfd on our loop */
        JanetStream *stream = make_stream(sfd, JANET_STREAM_ACCEPTABLE);
        return janet_wrap_abstract(stream);
    }
}

/* Types of socket's we need to deal with - relevant type puns below.
struct sockaddr *sa;           // Common base structure
struct sockaddr_storage *ss;   // Size of largest socket address type
struct sockaddr_in *sin;       // IPv4 address + port
struct sockaddr_in6 *sin6;     // IPv6 address + port
struct sockaddr_un *sun;       // Unix Domain Socket Address
*/

/* Turn a socket address into a host, port pair.
 * For unix domain sockets, returned tuple will have only a single element, the path string. */
static Janet janet_so_getname(const void *sa_any) {
    const struct sockaddr *sa = sa_any;
    char buffer[SA_ADDRSTRLEN];
    switch (sa->sa_family) {
        default:
            janet_panic("unknown address family");
        case AF_INET: {
            const struct sockaddr_in *sai = sa_any;
            if (!inet_ntop(AF_INET, &(sai->sin_addr), buffer, sizeof(buffer))) {
                janet_panic("unable to decode ipv4 host address");
            }
            Janet pair[2] = {janet_cstringv(buffer), janet_wrap_integer(ntohs(sai->sin_port))};
            return janet_wrap_tuple(janet_tuple_n(pair, 2));
        }
#ifndef JANET_NO_IPV6
        case AF_INET6: {
            const struct sockaddr_in6 *sai6 = sa_any;
            if (!inet_ntop(AF_INET6, &(sai6->sin6_addr), buffer, sizeof(buffer))) {
                janet_panic("unable to decode ipv4 host address");
            }
            Janet pair[2] = {janet_cstringv(buffer), janet_wrap_integer(ntohs(sai6->sin6_port))};
            return janet_wrap_tuple(janet_tuple_n(pair, 2));
        }
#endif
#ifndef JANET_WINDOWS
        case AF_UNIX: {
            const struct sockaddr_un *sun = sa_any;
            Janet pathname;
            if (sun->sun_path[0] == '\0') {
                memcpy(buffer, sun->sun_path, sizeof(sun->sun_path));
                buffer[0] = '@';
                pathname = janet_cstringv(buffer);
            } else {
                pathname = janet_cstringv(sun->sun_path);
            }
            return janet_wrap_tuple(janet_tuple_n(&pathname, 1));
        }
#endif
    }
}

JANET_CORE_FN(cfun_net_getsockname,
              "(net/localname stream)",
              "Gets the local address and port in a tuple in that order.") {
    janet_fixarity(argc, 1);
    JanetStream *js = janet_getabstract(argv, 0, &janet_stream_type);
    if (js->flags & JANET_STREAM_CLOSED) janet_panic("stream closed");
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    memset(&ss, 0, slen);
    if (getsockname((JSock)js->handle, (struct sockaddr *) &ss, &slen)) {
        janet_panicf("Failed to get localname on %v: %V", argv[0], janet_ev_lasterr());
    }
    janet_assert(slen <= (socklen_t) sizeof(ss), "socket address truncated");
    return janet_so_getname(&ss);
}

JANET_CORE_FN(cfun_net_getpeername,
              "(net/peername stream)",
              "Gets the remote peer's address and port in a tuple in that order.") {
    janet_fixarity(argc, 1);
    JanetStream *js = janet_getabstract(argv, 0, &janet_stream_type);
    if (js->flags & JANET_STREAM_CLOSED) janet_panic("stream closed");
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    memset(&ss, 0, slen);
    if (getpeername((JSock)js->handle, (struct sockaddr *)&ss, &slen)) {
        janet_panicf("Failed to get peername on %v: %V", argv[0], janet_ev_lasterr());
    }
    janet_assert(slen <= (socklen_t) sizeof(ss), "socket address truncated");
    return janet_so_getname(&ss);
}

JANET_CORE_FN(cfun_net_address_unpack,
              "(net/address-unpack address)",
              "Given an address returned by net/address, return a host, port pair. Unix domain sockets "
              "will have only the path in the returned tuple.") {
    janet_fixarity(argc, 1);
    struct sockaddr *sa = janet_getabstract(argv, 0, &janet_address_type);
    return janet_so_getname(sa);
}

JANET_CORE_FN(cfun_stream_accept_loop,
              "(net/accept-loop stream handler)",
              "Shorthand for running a server stream that will continuously accept new connections. "
              "Blocks the current fiber until the stream is closed, and will return the stream.") {
    janet_fixarity(argc, 2);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_ACCEPTABLE | JANET_STREAM_SOCKET);
    JanetFunction *fun = janet_getfunction(argv, 1);
    if (fun->def->min_arity < 1) janet_panic("handler function must take at least 1 argument");
    janet_sched_accept(stream, fun);
}

JANET_CORE_FN(cfun_stream_accept,
              "(net/accept stream &opt timeout)",
              "Get the next connection on a server stream. This would usually be called in a loop in a dedicated fiber. "
              "Takes an optional timeout in seconds, after which will raise an error. "
              "Returns a new duplex stream which represents a connection to the client.") {
    janet_arity(argc, 1, 2);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_ACCEPTABLE | JANET_STREAM_SOCKET);
    double to = janet_optnumber(argv, argc, 1, INFINITY);
    if (to != INFINITY) janet_addtimeout(to);
    janet_sched_accept(stream, NULL);
}

JANET_CORE_FN(cfun_stream_read,
              "(net/read stream nbytes &opt buf timeout)",
              "Read up to n bytes from a stream, suspending the current fiber until the bytes are available. "
              "`n` can also be the keyword `:all` to read into the buffer until end of stream. "
              "If less than n bytes are available (and more than 0), will push those bytes and return early. "
              "Takes an optional timeout in seconds, after which will raise an error. "
              "Returns a buffer with up to n more bytes in it, or raises an error if the read failed.") {
    janet_arity(argc, 2, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_READABLE | JANET_STREAM_SOCKET);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (janet_keyeq(argv[1], "all")) {
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_recvchunk(stream, buffer, INT32_MAX, MSG_NOSIGNAL);
    } else {
        int32_t n = janet_getnat(argv, 1);
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_recv(stream, buffer, n, MSG_NOSIGNAL);
    }
}

JANET_CORE_FN(cfun_stream_chunk,
              "(net/chunk stream nbytes &opt buf timeout)",
              "Same a net/read, but will wait for all n bytes to arrive rather than return early. "
              "Takes an optional timeout in seconds, after which will raise an error.") {
    janet_arity(argc, 2, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_READABLE | JANET_STREAM_SOCKET);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (to != INFINITY) janet_addtimeout(to);
    janet_ev_recvchunk(stream, buffer, n, MSG_NOSIGNAL);
}

JANET_CORE_FN(cfun_stream_recv_from,
              "(net/recv-from stream nbytes buf &opt timeout)",
              "Receives data from a server stream and puts it into a buffer. Returns the socket-address the "
              "packet came from. Takes an optional timeout in seconds, after which will raise an error.") {
    janet_arity(argc, 3, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_UDPSERVER | JANET_STREAM_SOCKET);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_getbuffer(argv, 2);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (to != INFINITY) janet_addtimeout(to);
    janet_ev_recvfrom(stream, buffer, n, MSG_NOSIGNAL);
}

JANET_CORE_FN(cfun_stream_write,
              "(net/write stream data &opt timeout)",
              "Write data to a stream, suspending the current fiber until the write "
              "completes. Takes an optional timeout in seconds, after which will raise an error. "
              "Returns nil, or raises an error if the write failed.") {
    janet_arity(argc, 2, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_WRITABLE | JANET_STREAM_SOCKET);
    double to = janet_optnumber(argv, argc, 2, INFINITY);
    if (janet_checktype(argv[1], JANET_BUFFER)) {
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_send_buffer(stream, janet_getbuffer(argv, 1), MSG_NOSIGNAL);
    } else {
        JanetByteView bytes = janet_getbytes(argv, 1);
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_send_string(stream, bytes.bytes, MSG_NOSIGNAL);
    }
}

JANET_CORE_FN(cfun_stream_send_to,
              "(net/send-to stream dest data &opt timeout)",
              "Writes a datagram to a server stream. dest is a the destination address of the packet. "
              "Takes an optional timeout in seconds, after which will raise an error. "
              "Returns stream.") {
    janet_arity(argc, 3, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_UDPSERVER | JANET_STREAM_SOCKET);
    void *dest = janet_getabstract(argv, 1, &janet_address_type);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (janet_checktype(argv[2], JANET_BUFFER)) {
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_sendto_buffer(stream, janet_getbuffer(argv, 2), dest, MSG_NOSIGNAL);
    } else {
        JanetByteView bytes = janet_getbytes(argv, 2);
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_sendto_string(stream, bytes.bytes, dest, MSG_NOSIGNAL);
    }
}

JANET_CORE_FN(cfun_stream_flush,
              "(net/flush stream)",
              "Make sure that a stream is not buffering any data. This temporarily disables Nagle's algorithm. "
              "Use this to make sure data is sent without delay. Returns stream.") {
    janet_fixarity(argc, 1);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_WRITABLE | JANET_STREAM_SOCKET);
    /* Toggle no delay flag */
    int flag = 1;
    setsockopt((JSock) stream->handle, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
    flag = 0;
    setsockopt((JSock) stream->handle, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
    return argv[0];
}

struct sockopt_type {
    const char *name;
    int level;
    int optname;
    enum JanetType type;
};

/* List of supported socket options; The type JANET_POINTER is used
 * for options that require special handling depending on the type. */
static const struct sockopt_type sockopt_type_list[] = {
    { "so-broadcast", SOL_SOCKET, SO_BROADCAST, JANET_BOOLEAN },
    { "so-reuseaddr", SOL_SOCKET, SO_REUSEADDR, JANET_BOOLEAN },
    { "so-keepalive", SOL_SOCKET, SO_KEEPALIVE, JANET_BOOLEAN },
    { "ip-multicast-ttl", IPPROTO_IP, IP_MULTICAST_TTL, JANET_NUMBER },
    { "ip-add-membership", IPPROTO_IP, IP_ADD_MEMBERSHIP, JANET_POINTER },
    { "ip-drop-membership", IPPROTO_IP, IP_DROP_MEMBERSHIP, JANET_POINTER },
#ifndef JANET_NO_IPV6
    { "ipv6-join-group", IPPROTO_IPV6, IPV6_JOIN_GROUP, JANET_POINTER },
    { "ipv6-leave-group", IPPROTO_IPV6, IPV6_LEAVE_GROUP, JANET_POINTER },
#endif
    { NULL, 0, 0, JANET_POINTER }
};

JANET_CORE_FN(cfun_net_setsockopt,
              "(net/setsockopt stream option value)",
              "set socket options.\n"
              "\n"
              "supported options and associated value types:\n"
              "- :so-broadcast boolean\n"
              "- :so-reuseaddr boolean\n"
              "- :so-keepalive boolean\n"
              "- :ip-multicast-ttl number\n"
              "- :ip-add-membership string\n"
              "- :ip-drop-membership string\n"
              "- :ipv6-join-group string\n"
              "- :ipv6-leave-group string\n") {
    janet_arity(argc, 3, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_SOCKET);
    JanetKeyword optstr = janet_getkeyword(argv, 1);

    const struct sockopt_type *st = sockopt_type_list;
    while (st->name) {
        if (janet_cstrcmp(optstr, st->name) == 0) {
            break;
        }
        st++;
    }

    if (st->name == NULL) {
        janet_panicf("unknown socket option %q", argv[1]);
    }

    union {
        int v_int;
        struct ip_mreq v_mreq;
#ifndef JANET_NO_IPV6
        struct ipv6_mreq v_mreq6;
#endif
    } val;

    void *optval = (void *)&val;
    socklen_t optlen = 0;

    if (st->type == JANET_BOOLEAN) {
        val.v_int = janet_getboolean(argv, 2);
        optlen = sizeof(val.v_int);
    } else if (st->type == JANET_NUMBER) {
        val.v_int = janet_getinteger(argv, 2);
        optlen = sizeof(val.v_int);
    } else if (st->optname == IP_ADD_MEMBERSHIP || st->optname == IP_DROP_MEMBERSHIP) {
        const char *addr = janet_getcstring(argv, 2);
        memset(&val.v_mreq, 0, sizeof val.v_mreq);
        val.v_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        inet_pton(AF_INET, addr, &val.v_mreq.imr_multiaddr.s_addr);
        optlen = sizeof(val.v_mreq);
#ifndef JANET_NO_IPV6
    } else if (st->optname == IPV6_JOIN_GROUP || st->optname == IPV6_LEAVE_GROUP) {
        const char *addr = janet_getcstring(argv, 2);
        memset(&val.v_mreq6, 0, sizeof val.v_mreq6);
        val.v_mreq6.ipv6mr_interface = 0;
        inet_pton(AF_INET6, addr, &val.v_mreq6.ipv6mr_multiaddr);
        optlen = sizeof(val.v_mreq6);
#endif
    } else {
        janet_panicf("invalid socket option type");
    }

    janet_assert(optlen != 0, "invalid socket option value");

    int r = setsockopt((JSock) stream->handle, st->level, st->optname, optval, optlen);
    if (r == -1) {
        janet_panicf("setsockopt(%q): %s", argv[1], janet_strerror(errno));
    }

    return janet_wrap_nil();
}

static const JanetMethod net_stream_methods[] = {
    {"chunk", cfun_stream_chunk},
    {"close", janet_cfun_stream_close},
    {"read", cfun_stream_read},
    {"write", cfun_stream_write},
    {"flush", cfun_stream_flush},
    {"accept", cfun_stream_accept},
    {"accept-loop", cfun_stream_accept_loop},
    {"send-to", cfun_stream_send_to},
    {"recv-from", cfun_stream_recv_from},
    {"evread", janet_cfun_stream_read},
    {"evchunk", janet_cfun_stream_chunk},
    {"evwrite", janet_cfun_stream_write},
    {"shutdown", cfun_net_shutdown},
    {"setsockopt", cfun_net_setsockopt},
    {NULL, NULL}
};

static JanetStream *make_stream(JSock handle, uint32_t flags) {
    return janet_stream((JanetHandle) handle, flags | JANET_STREAM_SOCKET, net_stream_methods);
}

void janet_lib_net(JanetTable *env) {
    JanetRegExt net_cfuns[] = {
        JANET_CORE_REG("net/address", cfun_net_sockaddr),
        JANET_CORE_REG("net/listen", cfun_net_listen),
        JANET_CORE_REG("net/accept", cfun_stream_accept),
        JANET_CORE_REG("net/accept-loop", cfun_stream_accept_loop),
        JANET_CORE_REG("net/read", cfun_stream_read),
        JANET_CORE_REG("net/chunk", cfun_stream_chunk),
        JANET_CORE_REG("net/write", cfun_stream_write),
        JANET_CORE_REG("net/send-to", cfun_stream_send_to),
        JANET_CORE_REG("net/recv-from", cfun_stream_recv_from),
        JANET_CORE_REG("net/flush", cfun_stream_flush),
        JANET_CORE_REG("net/connect", cfun_net_connect),
        JANET_CORE_REG("net/shutdown", cfun_net_shutdown),
        JANET_CORE_REG("net/peername", cfun_net_getpeername),
        JANET_CORE_REG("net/localname", cfun_net_getsockname),
        JANET_CORE_REG("net/address-unpack", cfun_net_address_unpack),
        JANET_CORE_REG("net/setsockopt", cfun_net_setsockopt),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, net_cfuns);
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
