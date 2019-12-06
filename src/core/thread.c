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

static JANET_THREAD_LOCAL JanetThreadSelector janet_vm_thread_selector;

void janet_threads_init(void) {
    pthread_mutex_init(&janet_vm_thread_selector.mutex, NULL);
    pthread_cond_init(&janet_vm_thread_selector.cond, NULL);
    janet_vm_thread_selector.channel = NULL;
}

void janet_threads_deinit(void) {
    pthread_mutex_destroy(&janet_vm_thread_selector.mutex);
    pthread_cond_destroy(&janet_vm_thread_selector.cond);
    janet_vm_thread_selector.channel = NULL;
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
    channel->selector = NULL;
    channel->refCount = 2;
    channel->encode = NULL;
    channel->decode = NULL;
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
        return 0;
    }
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
    JanetChannel *rx = thread->rx;
    JanetChannel *tx = thread->tx;
    if (tx && tx->encode) {
        janet_mark(janet_wrap_table(tx->encode));
    }
    if (rx && rx->encode) {
        janet_mark(janet_wrap_table(rx->decode));
    }
    return 0;
}

/* Returns 1 if could not send, but do not panic or block (for long). */
static int janet_channel_send(JanetChannel *tx, Janet msg) {
    JanetThreadSelector *selector = tx->selector;

    /* Check for closed channel */
    if (tx->refCount <= 1) return 1;

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;
    int32_t oldcount = tx->buf.count;

    int ret = 0;
    if (setjmp(buf)) {
        ret = 1;
        tx->buf.count = oldcount;
    } else {
        janet_marshal(&tx->buf, msg, tx->encode, 0);
        if (selector) {
            pthread_mutex_lock(&selector->mutex);
            if (!selector->channel) {
                selector->channel = tx;
                pthread_cond_signal(&selector->cond);
            }
            pthread_mutex_unlock(&selector->mutex);
        }
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;

    return ret;
}

/* Returns 0 on successful message.
 * Returns 1 if nothing in queue or failed to get item. In this case,
 * also sets the channel's selector value.
 * Returns 2 if channel closed.
 * Does not block (for long) or panic, and sets the channel's selector
 * . */
static int janet_channel_receive(JanetChannel *rx, Janet *msg_out) {

    /* Check for no messages */
    while (rx->buf.count == 0) {
        int is_dead = rx->refCount <= 1;
        rx->selector = &janet_vm_thread_selector;
        return is_dead ? 2 : 1;
    }

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;

    /* Handle errors */
    int ret = 0;
    if (setjmp(buf)) {
        rx->buf.count = 0;
        rx->selector = &janet_vm_thread_selector;
        ret = 1;
    } else {
        /* Read from beginning of channel */
        const uint8_t *nextItem = NULL;
        Janet item = janet_unmarshal(rx->buf.data, rx->buf.count,
                                     0, rx->decode, &nextItem);

        /* Update memory and put result into *msg_out */
        int32_t chunkCount = nextItem - rx->buf.data;
        memmove(rx->buf.data, nextItem, rx->buf.count - chunkCount);
        rx->buf.count -= chunkCount;
        *msg_out = item;

        /* Got message, unset selector */
        rx->selector = NULL;
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;

    return ret;
}

/* Get a message from one of the channels given. */
static int janet_channel_select(int32_t n, JanetChannel **rxs, Janet *msg_out) {
    int32_t maxChannel = -1;
    for (;;) {
        janet_vm_thread_selector.channel = NULL;

        /* Try each channel, first without acquiring locks and looking
         * only for existing messages, then with acquiring
         * locks, which will not miss messages. */
        for (int trylock = 1; trylock >= 0; trylock--) {
            for (int32_t i = 0; i < n; i++) {
                JanetChannel *rx = rxs[i];
                if (trylock) {
                    if (rx->buf.count == 0 || pthread_mutex_trylock(&rx->lock)) continue;
                } else {
                    pthread_mutex_lock(&rxs[i]->lock);
                }
                int status = janet_channel_receive(rxs[i], msg_out);
                pthread_mutex_unlock(&rxs[i]->lock);
                if (status == 0) goto gotMessage;
                maxChannel = maxChannel > i ? maxChannel : i;
                if (status == 2) {
                    /* channel closed and will receive no more messages, drop it */
                    rxs[i] = rxs[--n];
                    --i;
                }
            }
        }

        /* All channels closed */
        if (n == 0) return 1;

        pthread_mutex_lock(&janet_vm_thread_selector.mutex);
        {
            /* Wait until we have a channel */
            if (NULL == janet_vm_thread_selector.channel) {
                pthread_cond_wait(
                    &janet_vm_thread_selector.cond,
                    &janet_vm_thread_selector.mutex);
            }

            /* Got channel, swap it with first channel, and
             * then go back to receiving messages. */
            JanetChannel *rx = janet_vm_thread_selector.channel;
            int32_t index = 0;
            while (rxs[index] != rx) index++;
            rxs[index] = rxs[0];
            rxs[0] = rx;
        }
        pthread_mutex_unlock(&janet_vm_thread_selector.mutex);
    }

gotMessage:
    /* got message, unset selectors and return */
    for (int32_t j = 0; j <= maxChannel && j < n; j++) {
        pthread_mutex_lock(&rxs[j]->lock);
        rxs[j]->selector = NULL;
        pthread_mutex_unlock(&rxs[j]->lock);
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
    rx->decode = decode;
    tx->encode = encode;
    return thread;
}

JanetThread *janet_getthread(const Janet *argv, int32_t n) {
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
    JanetThread *thread = janet_make_thread(rx, tx, encode, decode);
    Janet threadv = janet_wrap_abstract(thread);

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_channel_select(1, &rx, &funcv);
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
    janet_fixarity(argc, 1);
    int status;
    Janet out = janet_wrap_nil();
    int32_t count;
    const Janet *items;
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
        status = janet_channel_select(realcount, rxs, &out);
        if (rxs != rxs_stack) janet_sfree(rxs);
    } else {
        /* Get from one thread */
        JanetThread *thread = janet_getthread(argv, 0);
        if (NULL == thread->rx) janet_panic("channel has closed");
        status = janet_channel_select(1, &thread->rx, &out);
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
        JDOC("(thread/receive threads)\n\n"
             "Get a value sent to 1 or more threads. Will block if no value was sent to this thread "
             "yet. threads can also be an array or tuple of threads, in which case "
             "thread/receive will select on the first thread to return a value. Returns "
             "the message sent to the thread.")
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
