/*
* Copyright (c) 2019 Calvin Rose
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
#include <janet.h>
#include "gc.h"
#include "util.h"
#include "state.h"
#endif

#ifdef JANET_THREADS

#include <setjmp.h>
#include <time.h>
#include <pthread.h>

/* Global data */
static pthread_rwlock_t janet_g_lock = PTHREAD_RWLOCK_INITIALIZER;
static JanetMailbox **janet_g_mailboxes = NULL;
static size_t janet_g_mailboxes_cap = 0;
static size_t janet_g_mailboxes_count = 0;
static uint64_t janet_g_next_mailbox_id = 0;

/* typedefed in janet.h */
struct JanetMailbox {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint64_t id;
    JanetBuffer buf;
    int refCount;
    int closed;
};

static JANET_THREAD_LOCAL JanetMailbox *janet_vm_mailbox;

void janet_threads_init(void) {
    janet_vm_mailbox = malloc(sizeof(JanetMailbox));
    if (NULL == janet_vm_mailbox) {
        JANET_OUT_OF_MEMORY;
    }
    pthread_mutex_init(&janet_vm_mailbox->lock, NULL);
    pthread_cond_init(&janet_vm_mailbox->cond, NULL);
    janet_buffer_init(&janet_vm_mailbox->buf, 1024);
    janet_vm_mailbox->refcount = 1;
    janet_vm_mailbox->closed = 0;

    /* Add mailbox to global table */
    pthread_rwlock_wrlock(&janet_janet_lock);
    janet_vm_mailbox->id = janet_g_next_mailbox_id++;
    size_t newcount = janet_g_mailboxes_count + 1;
    if (janet_g_mailboxes_cap < newcount) {
        size_t newcap = newcount * 2;
        JanetMailbox **mailboxes = realloc(janet_g_mailboxes, newcap * sizeof(JanetMailbox *));
        if (NULL == mailboxes) {
            pthread_rwlock_unlock(&janet_janet_lock);
            /* this maybe should be a different error, as this basically means
             * we cannot create a new thread. So janet_init should probably fail. */
            JANET_OUT_OF_MEMORY;
            return;
        }
        janet_g_mailboxes = mailboxes;
        janet_g_mailboxes_cap = newcap;
    }
    janet_g_mailboxes[janet_g_mailboxes_count] = janet_vm_mailbox;
    janet_g_mailboxes_count = newcount;
    pthread_rwlock_unlock(&janet_janet_lock);
}

static void janet_mailbox_ref(JanetMailbox *mailbox) {
    pthread_mutex_lock(&mailbox->lock);
    mailbox->refCount++;
    pthread_mutex_unlock(&mailbox->lock);
}

static JanetMailbox *janet_find_mailbox(uint64_t id, int remove) {
    JanetMailbox *ret = NULL;
    if (remove) {
        pthread_rwlock_wrlock(&janet_janet_lock);
    } else {
        pthread_rwlock_rdlock(&janet_janet_lock);
    }
    size_t i = 0;
    while (i < janet_g_mailboxes_count && janet_g_mailboxes[i]->id != mailbox->id)
        i++;
    if (i < janet_g_mailboxes_count) {
        ret = janet_g_mailboxes[i];
        if (remove) {
            janet_g_mailboxes[i] = janet_g_mailboxes[--janet_g_mailboxes_count];
        }
    }
    pthread_rwlock_unlock(&janet_janet_lock);
    return ret;
}

/* Assumes you have the mailbox lock already */
static void janet_mailbox_deref_with_lock(JanetMailbox *mailbox) {
    if (mailbox->refCount <= 1) {
        /* We are the last reference */
        pthread_mutex_destroy(&mailbox->lock);
        pthread_mutex_destroy(&mailbox->cond);
        janet_buffer_deinit(&mailbox->buf);
        janet_find_mailbox(mailbox->id, 1);
        free(mailbox);
    } else {
        /* There are other references */
        if (mailbox == janet_vm_mailbox) {
            /* We own this mailbox, so mark it as closed for other references. */
            mailbox->closed = 1;
        }
        janet_vm_mailbox->refCount--;
        pthread_mutex_unlock(&mailbox->lock);
    }
}

static void janet_mailbox_deref(JanetMailbox *mailbox) {
    pthread_mutex_lock(&mailbox->lock);
    janet_mailbox_deref_with_lock(mailbox);
}

void janet_threads_deinit(void) {
    janet_mailbox_deref(janet_vm_mailbox);
    janet_vm_mailbox = NULL;
}

static JanetTable *janet_get_core_table(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    JanetBindingType bt = janet_resolve(env, janet_csymbol(name), &out);
    if (bt == JANET_BINDING_NONE) return NULL;
    if (!janet_checktype(out, JANET_TABLE)) return NULL;
    return janet_unwrap_table(out);
}

static void janet_close_thread(JanetThread *thread) {
    if (thread->mailbox) {
        janet_mailbox_deref(thread->mailbox);
        thread->mailbox = NULL;
    }
}

static int thread_gc(void *p, size_t size) {
    (void) size;
    JanetThread *thread = (JanetThread *)p;
    janet_close_thread(thread);
    return 0;
}

static int thread_mark(void *p, size_t size) {
    JanetThread *thread = (JanetThread *)p;
    if (thread->encode) {
        janet_mark(janet_wrap_table(thread->encode));
    }
    return 0;
}

/* Returns 1 if could not send (encode error), 2 for mailbox closed, and
 * 0 otherwise. Will not panic.  */
int janet_thread_send(JanetThread *thread, Janet msg) {

    /* Ensure mailbox is not closed. */
    JanetMailbox *mailbox = thread->mailbox;
    if (NULL == mailbox) return 2;
    pthread_mutex_lock(mailbox->lock);
    if (mailbox->closed) {
        janet_mailbox_deref_with_lock(mailbox);
        thread->mailbox  == NULL;
        return 2;
    }

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;
    int32_t oldcount = mailbox->buf.count;

    int ret = 0;
    if (setjmp(buf)) {
        ret = 1;
        mailbox->buf.count = oldcount;
    } else {
        janet_marshal(&mailbox->buf, msg, thread->encode, 0);
        if (oldcount == 0) {
            pthread_cond_signal(&mailbox->cond);
        }
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;

    return ret;
}

/* Convert an interval from now in an absolute timespec */
static void janet_sec2ts(double sec, struct timespec *ts) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    time_t tvsec = (time_t) floor(sec);
    long tvnsec = (long) floor(1000000000.0 * (sec - ((double) tvsec)));
    tvsec += now.tv_sec;
    tvnsec += now.tv_nsec;
    if (tvnsec >= 1000000000L) {
        tvnsec -= 1000000000L;
        tvsec += 1;
    }
    ts->tv_sec = tvsec;
    ts->tv_nsec = tvnsec;
}

/* Returns 0 on successful message.
 * Returns 1 if nothing in queue or failed to get item. In this case,
 * also sets the channel's selector value.
 * Returns 2 if channel closed.
 * . */
int janet_thread_receive(Janet *msg_out, double timeout, JanetTable *decode) {
    pthread_mutex_lock(&janet_vm_mailbox->lock);

    /* For timeouts */
    struct timespec timeout_ts;
    int timedwait = timeout > 0.0;
    int nowait = timeout == 0.0;
    if (timedwait) janet_sec2ts(timeout, &timeout_ts);

    for (;;) {

        /* Check for messages waiting for use */
        if (janet_vm_mailbox->buf.count) {
            /* Hack to capture all panics from marshalling. This works because
             * we know janet_marshal won't mess with other essential global state. */
            jmp_buf buf;
            jmp_buf *old_buf = janet_vm_jmp_buf;
            janet_vm_jmp_buf = &buf;

            /* Handle errors */
            if (setjmp(buf)) {
                /* Bad message, so clear buffer and wait for the next */
                janet_vm_mailbox->buf.count = 0;
                janet_vm_jmp_buf = old_buf;
            } else {
                /* Read from beginning of channel */
                const uint8_t *nextItem = NULL;
                Janet item = janet_unmarshal(
                        janet_vm_mailbox->buf.data, janet_vm_mailbox->buf.count,
                        0, rx->decode, &nextItem);

                /* Update memory and put result into *msg_out */
                int32_t chunkCount = nextItem - janet_vm_mailbox->buf.data;
                memmove(janet_vm_mailbox->buf.data, nextItem, janet_vm_mailbox->buf.count - chunkCount);
                janet_vm_mailbox->buf.count -= chunkCount;
                *msg_out = item;
                janet_vm_jmp_buf = old_buf;
                pthread_mutex_unlock(&janet_vm_mailbox->lock);
                return 0;
            }
        }

        if (nowait) {
            pthread_mutex_unlock(&janet_vm_mailbox->lock);
            return 1;
        }

        /* Wait for next message */
        if (timedwait) {
           if (pthread_cond_timedwait(
                    &janet_vm_mailbox->cond,
                    &janet_vm_mailbox->mutex,
                    &timeout_ts)) {
               return 1; /* Timeout */
           }
        } else {
            pthread_cond_wait(
                    &janet_vm_mailbox->cond,
                    &janet_vm_mailbox->mutex);
        }
    }

}

static Janet janet_thread_getter(void *p, Janet key);

static JanetAbstractType Thread_AT = {
    "core/thread",
    thread_gc,
    thread_mark,
    janet_thread_getter,
    NULL,
    NULL,
    NULL,
    NULL
};

static JanetThread *janet_make_thread(JanetMailbox *mailbox, Janet *encode) {
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    janet_mailbox_ref(mailbox)
    thread->mailbox = mailbox;
    thread->encode = encode;
    return thread;
}

JanetThread *janet_getthread(const Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &Thread_AT);
}

/* Runs in new thread */
static int thread_worker(JanetMailbox *mailbox) {
    /* Init VM */
    janet_init();

    /* Get dictionaries */
    JanetTable *encode = janet_get_core_table("make-image-dict");
    JanetTable *decode = janet_get_core_table("load-image-dict");

    /* Create self thread */
    JanetThread *thread = janet_make_thread(mailbox, encode);
    Janet threadv = janet_wrap_abstract(thread);

    /* Send pointer to current mailbox to parent */

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_thread_receive(&funcv, -1.0, decode);
    if (status) goto error;
    if (!janet_checktype(funcv, JANET_FUNCTION)) goto error;
    JanetFunction *func = janet_unwrap_function(funcv);

    /* Arity check */
    if (func->def->min_arity > 1 || func->def->max_arity < 1) {
        goto error;
    }

    /* Call function */
    Janet argv[1] = { threadv };
    JanetFiber *fiber = janet_fiber(func, 64, 1, argv);
    Janet out;
    janet_continue(fiber, janet_wrap_nil(), &out);

    /* Success */
    janet_deinit();
    return 0;

    /* Fail */
error:
    janet_deinit();
    return 1;
}

static void *janet_pthread_wrapper(void *param) {
    thread_worker((JanetChannel *)param);
    return NULL;
}

static int janet_thread_start_child(JanetThread *thread) {
    pthread_t handle;
    /* My rx is your tx and vice versa */
    int error = pthread_create(&handle, NULL, janet_pthread_wrapper, thread->rx);
    if (error) {
        /* double close as there is no other side to close thread */
        janet_close_thread(thread);
        janet_close_thread(thread);
        return 1;
    } else {
        pthread_detach(handle);
        return 0;
    }
}

/*
 * Cfuns
 */

static Janet cfun_thread_new(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 2);
    JanetTable *encode = (argc < 1 || janet_checktype(argv[0], JANET_NIL))
                         ? janet_get_core_table("make-image-dict")
                         : janet_gettable(argv, 0);
    JanetTable *decode = (argc < 2 || janet_checktype(argv[1], JANET_NIL))
                         ? janet_get_core_table("load-image-dict")
                         : janet_gettable(argv, 1);
    JanetChannel *rx = malloc(2 * sizeof(JanetChannel));
    if (NULL == rx) {
        JANET_OUT_OF_MEMORY;
    }
    JanetChannel *tx = rx + 1;
    janet_channel_init(rx);
    janet_channel_init(tx);
    JanetThread *thread = janet_make_thread(rx, tx, encode, decode);
    if (janet_thread_start_child(thread))
        janet_panic("could not start thread");
    return janet_wrap_abstract(thread);
}

static Janet cfun_thread_send(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetChannel *tx = janet_getthread(argv, 0)->tx;
    if (NULL == tx) janet_panic("channel has closed");
    pthread_mutex_lock(&tx->lock);
    int status = janet_channel_send(tx, argv[1]);
    pthread_mutex_unlock(&tx->lock);
    if (status) {
        janet_panicf("failed to send message %v", argv[1]);
    }
    return argv[0];
}

static Janet cfun_thread_receive(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    int status;
    Janet out = janet_wrap_nil();
    int32_t count;
    const Janet *items;
    double wait = janet_optnumber(argv, argc, 1, -1.0);
    if (janet_indexed_view(argv[0], &items, &count)) {
        /* Select on multiple threads */
        if (count == 0) janet_panic("expected at least 1 thread");
        int32_t realcount = 0;
        JanetChannel *rxs_stack[10] = {NULL};
        JanetChannel **rxs = (count > 10)
                             ? janet_smalloc(count * sizeof(JanetChannel *))
                             : rxs_stack;
        for (int32_t i = 0; i < count; i++) {
            JanetThread *thread = janet_getthread(items, i);
            if (thread->rx != NULL) rxs[realcount++] = thread->rx;
        }
        status = janet_channel_select(realcount, rxs, &out, wait);
        if (rxs != rxs_stack) janet_sfree(rxs);
    } else {
        /* Get from one thread */
        JanetThread *thread = janet_getthread(argv, 0);
        if (NULL == thread->rx) janet_panic("channel has closed");
        status = janet_channel_select(1, &thread->rx, &out, wait);
    }
    if (status) {
        janet_panic("failed to receive message");
    }
    return out;
}

static Janet cfun_thread_close(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetThread *thread = janet_getthread(argv, 0);
    janet_close_thread(thread);
    return janet_wrap_nil();
}

static const JanetMethod janet_thread_methods[] = {
    {"send", cfun_thread_send},
    {"receive", cfun_thread_receive},
    {"close", cfun_thread_close},
    {NULL, NULL}
};

static Janet janet_thread_getter(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) janet_panicf("expected keyword method");
    return janet_getmethod(janet_unwrap_keyword(key), janet_thread_methods);
}

static const JanetReg threadlib_cfuns[] = {
    {
        "thread/new", cfun_thread_new,
        JDOC("(thread/new &opt encode-book decode-book)\n\n"
             "Start a new thread. The thread will wait for a message containing the function used to start the thread, which should be subsequently "
             "sent over after thread creation.")
    },
    {
        "thread/send", cfun_thread_send,
        JDOC("(thread/send thread msg)\n\n"
             "Send a message to the thread. This will never block and returns thread immediately. "
             "Will throw an error if there is a problem sending the message.")
    },
    {
        "thread/receive", cfun_thread_receive,
        JDOC("(thread/receive threads &opt timeout)\n\n"
             "Get a value sent to 1 or more threads. Will block if no value was sent to this thread "
             "yet. threads can also be an array or tuple of threads, in which case "
             "thread/receive will select on the first thread to return a value. Returns "
             "the message sent to the thread. If a timeout (in seconds) is provided, failure "
             "to get a message will throw an error after the timeout has elapsed.")
    },
    {
        "thread/close", cfun_thread_close,
        JDOC("(thread/close thread)\n\n"
             "Close a thread, unblocking it and ending communication with it. Note that closing "
             "a thread is idempotent and does not cancel the thread's operation. Returns nil.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_thread(JanetTable *env) {
    janet_core_cfuns(env, NULL, threadlib_cfuns);
}

#endif
