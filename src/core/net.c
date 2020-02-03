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
 * Event loops
 */

/* This large struct describes a waiting file descriptor, as well
 * as what to do when we get an event for it. */
typedef struct {

    /* File descriptor to listen for events on. */
    int fd;

    /* Fiber to resume when event finishes. Can be NULL. */
    JanetFiber *fiber;

    /* We need to tell which fd_set to put in for select. */
    enum {
        JLFD_READ,
        JLFD_WRITE
    } select_mode;

    /* What kind of event we are listening for.
     * As more IO functionality get's added, we can
     * expand this. */
    enum {
        JLE_READ_INTO_BUFFER,
        JLE_READ_ACCEPT,
        JLE_WRITE_FROM_BUFFER,
        JLE_WRITE_FROM_STRINGLIKE
    } event_type;

    union {

        /* JLE_READ_INTO_BUFFER */
        struct {
            int32_t n;
            JanetBuffer *buf;
        } read_into_buffer;

        /* JLE_READ_ACCEPT */
        struct {
            JanetFunction *handler;
        } read_accept;

        /* JLE_WRITE_FROM_BUFFER */
        struct {
            JanetBuffer *buf;
        } write_from_buffer;

        /* JLE_WRITE_FROM_STRINGLIKE */
        struct {
            const uint8_t *str;
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
        switch (lfd.event_type) {
            default:
                break;
            case JLE_READ_INTO_BUFFER:
                janet_mark(janet_wrap_buffer(lfd.data.read_into_buffer.buf));
                break;
            case JLE_READ_ACCEPT:
                janet_mark(janet_wrap_function(lfd.data.read_accept.handler));
                break;
            case JLE_WRITE_FROM_BUFFER:
                janet_mark(janet_wrap_buffer(lfd.data.write_from_buffer.buf));
                break;
            case JLE_WRITE_FROM_STRINGLIKE:
                janet_mark(janet_wrap_buffer(lfd.data.write_from_buffer.buf));
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
    if (NULL != lfd.fiber) {
        janet_gcroot(janet_wrap_fiber(lfd.fiber));
    }
    return index;
}

/* Remove an event listener by the handle it returned when scheduled. */
static void janet_loop_unschedule(int index) {
    janet_vm_loopfds[index] = janet_vm_loopfds[--janet_vm_loop_count];
}

/* Return delta in number of loop fds. Abstracted out so
 * we can separate out the polling logic */
static size_t janet_loop_event(size_t index) {
    JanetLoopFD *jlfd = janet_vm_loopfds + index;
    int ret = 1;
    int should_resume = 0;
    Janet resumeval = janet_wrap_nil();
    switch (jlfd->event_type) {
        case JLE_READ_INTO_BUFFER:
            {
                JanetBuffer *buffer = jlfd->data.read_into_buffer.buf;
                int32_t how_much = jlfd->data.read_into_buffer.n;
                janet_buffer_extra(buffer, how_much);
                int status = read(jlfd->fd, buffer->data + buffer->count, how_much);
                if (status > 0) {
                    buffer->count += how_much;
                }
                should_resume = 1;
                resumeval = janet_wrap_buffer(buffer);
                /* Bag pop */
                janet_loop_unschedule(index);
                ret = 0;
                break;
            }
        case JLE_READ_ACCEPT:
            {
                char addr[256]; /* Just make sure it is large enough for largest address type */
                socklen_t len;
                int connfd = accept(jlfd->fd, (void *) &addr, &len);
                if (connfd >= 0) {
                    /* Made a new connection socket */
                    int flags = fcntl(connfd, F_GETFL, 0);
                    fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
                    FILE *f = fdopen(connfd, "r+");
                    Janet filev = janet_makefile(f, JANET_FILE_WRITE | JANET_FILE_READ);
                    JanetFunction *handler = jlfd->data.read_accept.handler;
                    Janet out;
                    /* Launch connection fiber */
                    janet_pcall(handler, 1, &filev, &out, NULL);
                }
                ret = 1;
                break;
            }
        case JLE_WRITE_FROM_BUFFER:
        case JLE_WRITE_FROM_STRINGLIKE:
            ret = 1;
            break;
    }
    if (NULL != jlfd->fiber && should_resume) {
        /* Resume the fiber */
        Janet out;
        janet_continue(jlfd->fiber, resumeval, &out);
    }
    return ret;
}

void janet_loop1(void) {
    /* Set up fd_sets */
    fd_set readfds;
    fd_set writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    int fd_max = 0;
    for (int i = 0; i < janet_vm_loop_count; i++) {
        JanetLoopFD *jlfd = janet_vm_loopfds + i;
        if (jlfd->fd > fd_max) fd_max = jlfd->fd;
        fd_set *set = (jlfd->select_mode == JLFD_READ) ? &readfds : &writefds;
        FD_SET(jlfd->fd, set);
    }

    /* Blocking call - we should add timeout functionality */
    printf("selecting %d!\n", janet_vm_loop_count);
    int status = select(fd_max, &readfds, &writefds, NULL, NULL);
    (void) status;
    printf("selected!\n");

    /* Now handle all events */
    for (int i = 0; i < janet_vm_loop_count;) {
        JanetLoopFD *jlfd = janet_vm_loopfds + i;
        fd_set *set = (jlfd->select_mode == JLFD_READ) ? &readfds : &writefds;
        if (FD_ISSET(jlfd->fd, set)) {
            size_t delta = janet_loop_event(i);
            i += delta;
        } else {
            i++;
        }
    }
}

void janet_loop(void) {
    while (janet_vm_loop_count) janet_loop1();
}

/*
 * C Funs
 */

static Janet cfun_net_server(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);

    /* Get host, port, and handler*/
    const char *host = janet_getcstring(argv, 0);
    const char *port = janet_getcstring(argv, 1);
    JanetFunction *fun = janet_getfunction(argv, 2);

    /* getaddrinfo */
    struct addrinfo *ai;
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_PASSIVE;
    int status = getaddrinfo(host, port, &hints, &ai);
    if (status) {
        janet_panicf("could not get address info: %s", gai_strerror(status));
    }

    /* bind */
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
    status = listen(sfd, 1024);
    if (status) {
        freeaddrinfo(ai);
        close(sfd);
        janet_panic("could not listen on file descriptor");
    }

    /* We need to ignore sigpipe when reading and writing to our connection socket.
     * Since a connection could be disconnected at any time, any read or write may fail.
     * We don't want to blow up the whole application. */
    signal(SIGPIPE, SIG_IGN);

    /* cleanup */
    freeaddrinfo(ai);

    /* Put sfd on our loop */
    JanetLoopFD lfd = {0};
    lfd.fd = sfd;
    lfd.select_mode = JLFD_READ;
    lfd.event_type = JLE_READ_ACCEPT;
    lfd.data.read_accept.handler = fun;
    janet_loop_schedule(lfd);

    return janet_wrap_nil();
}

static Janet cfun_net_loop(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    printf("starting loop...\n");
    janet_loop();
    return janet_wrap_nil();
}

static const JanetReg net_cfuns[] = {
    {"net/server", cfun_net_server,
        JDOC("(net/server host port)\n\nStart a simple TCP echo server.")},
    {"net/loop", cfun_net_loop, NULL},
    {NULL, NULL, NULL}
};

void janet_lib_net(JanetTable *env) {
    janet_vm_loop_count = 0;
    janet_core_cfuns(env, NULL, net_cfuns);
}

