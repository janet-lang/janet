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

JANET_THREAD_LOCAL pthread_cond_t janet_vm_thread_cond;

void janet_threads_init(void) {
    pthread_cond_init(&janet_vm_thread_cond, NULL);
}

void janet_threads_deinit(void) {
    pthread_cond_destroy(&janet_vm_thread_cond);
}

static JanetTable *janet_get_core_table(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    JanetBindingType bt = janet_resolve(env, janet_csymbol(name), &out);
    if (bt == JANET_BINDING_NONE) return NULL;
    if (!janet_checktype(out, JANET_TABLE)) return NULL;
    return janet_unwrap_table(out);
}

static void janet_channel_init(JanetChannel *channel) {
    janet_buffer_init(&channel->buf, 0);
    pthread_mutex_init(&channel->lock, NULL);
    channel->rx_cond = NULL;
    channel->refCount = 2;
    channel->mailboxFlag = 0;
}

/* Return 1 if channel memory should be freed, otherwise 0 */
static int janet_channel_deref(JanetChannel *channel) {
    pthread_mutex_lock(&channel->lock);
    if (1 == channel->refCount) {
        janet_buffer_deinit(&channel->buf);
        pthread_mutex_destroy(&channel->lock);
        return 1;
    } else {
        channel->refCount--;
        pthread_mutex_unlock(&channel->lock);
        /* Wake up other side if they are blocked, otherwise
         * they will block forever. */
        if (NULL != channel->rx_cond) {
            pthread_cond_signal(channel->rx_cond);
        }
        return 0;
    }
}

/* Returns 1 if could not send. Does not block or panic. Bytes should be a janet value that
 * has been marshalled. */
static int janet_channel_send(JanetChannel *channel, Janet msg, JanetTable *dict) {
    pthread_mutex_lock(&channel->lock);

    /* Check for closed channel */
    if (channel->refCount <= 1) return 1;

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;
    int32_t oldcount = channel->buf.count;

    int ret = 0;
    if (setjmp(buf)) {
        ret = 1;
        channel->buf.count = oldcount;
    } else {
        janet_marshal(&channel->buf, msg, dict, 0);

        /* Was empty, signal to cond */
        if (oldcount == 0 && (NULL != channel->rx_cond)) {
            pthread_cond_signal(channel->rx_cond);
        }
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;
    pthread_mutex_unlock(&channel->lock);

    return ret;
}

/* Returns 1 if nothing in queue or failed to get item. Does not block or panic. Uses dict to read bytes from
 * the channel and unmarshal them. */
static int janet_channel_receive(JanetChannel *channel, Janet *msg_out, JanetTable *dict) {
    pthread_mutex_lock(&channel->lock);

    /* If queue is empty, block for now. */
    while (channel->buf.count == 0) {
        /* Check for closed channel (1 ref left means other side quit) */
        if (channel->refCount <= 1) return 1;
        /* Since each thread sets its own rx_cond, we know it's not NULL */
        pthread_cond_wait(channel->rx_cond, &channel->lock);
    }

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;

    /* Handle errors */
    int ret = 0;
    if (setjmp(buf)) {
        /* Clear the channel on errors */
        channel->buf.count = 0;
        ret = 1;
    } else {
        /* Read from beginning of channel */
        const uint8_t *nextItem = NULL;
        Janet item = janet_unmarshal(channel->buf.data, channel->buf.count, 0, dict, &nextItem);

        /* Update memory and put result into *msg_out */
        int32_t chunkCount = nextItem - channel->buf.data;
        memmove(channel->buf.data, nextItem, channel->buf.count - chunkCount);
        channel->buf.count -= chunkCount;
        *msg_out = item;
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;
    pthread_mutex_unlock(&channel->lock);

    return ret;
}

static void janet_close_thread(JanetThread *thread) {
    if (NULL != thread->rx) {
        JanetChannel *rx = thread->rx;
        JanetChannel *tx = thread->tx;
        /* Deref both. The reference counts should be in sync. */
        janet_channel_deref(rx);
        if (janet_channel_deref(tx)) {
            /* tx and rx were allocated together. free the one with the lower address. */
            free(rx < tx ? rx : tx);
        }
        thread->rx = NULL;
        thread->tx = NULL;
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
    (void) size;
    if (NULL != thread->encode) {
        janet_mark(janet_wrap_table(thread->encode));
    }
    if (NULL != thread->decode) {
        janet_mark(janet_wrap_table(thread->decode));
    }
    return 0;
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

static JanetThread *janet_make_thread(JanetChannel *rx, JanetChannel *tx, JanetTable *encode, JanetTable *decode) {
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    thread->rx = rx;
    thread->tx = tx;
    thread->encode = encode;
    thread->decode = decode;
    return thread;
}

JanetThread *janet_getthread(Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &Thread_AT);
}

/* Runs in new thread */
static int thread_worker(JanetChannel *tx) {
    /* Init VM */
    janet_init();

    /* Get dictionaries */
    JanetTable *decode = janet_get_core_table("load-image-dict");
    JanetTable *encode = janet_get_core_table("make-image-dict");

    /* Create self thread */
    JanetChannel *rx = tx + 1;
    rx->rx_cond = &janet_vm_thread_cond;
    JanetThread *thread = janet_make_thread(rx, tx, encode, decode);
    Janet threadv = janet_wrap_abstract(thread);

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_channel_receive(rx, &funcv, decode);
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
    rx->rx_cond = &janet_vm_thread_cond;
    JanetThread *thread = janet_make_thread(rx, tx, encode, decode);
    if (janet_thread_start_child(thread))
        janet_panic("could not start thread");
    return janet_wrap_abstract(thread);
}

static Janet cfun_thread_send(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetThread *thread = janet_getthread(argv, 0);
    if (NULL == thread->tx) janet_panic("channel has closed");
    int status = janet_channel_send(thread->tx, argv[1], thread->encode);
    if (status) {
        janet_panicf("failed to send message %v", argv[1]);
    }
    return argv[0];
}

static Janet cfun_thread_receive(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetThread *thread = janet_getthread(argv, 0);
    if (NULL == thread->rx) janet_panic("channel has closed");
    Janet out = janet_wrap_nil();
    int status = janet_channel_receive(thread->rx, &out, thread->decode);
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
        JDOC("(thread/receive thread)\n\n"
             "Get a value sent to thread. Will block if there is no value was sent to this thread yet. Returns the message sent to the thread.")
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
