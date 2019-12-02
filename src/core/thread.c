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

#include <pthread.h>
#include <setjmp.h>

static JanetTable *janet_get_core_table(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    JanetBindingType bt = janet_resolve(env, janet_csymbol(name), &out);
    if (bt == JANET_BINDING_NONE) return NULL;
    if (!janet_checktype(out, JANET_TABLE)) return NULL;
    return janet_unwrap_table(out);
}

static void janet_channel_init(JanetChannel *channel, size_t initialSize) {
    janet_buffer_init(&channel->buf, (int32_t) initialSize);
    pthread_mutex_init(&channel->lock, NULL);
    pthread_cond_init(&channel->cond, NULL);
}

static void janet_channel_destroy(JanetChannel *channel) {
    janet_buffer_deinit(&channel->buf);
    pthread_mutex_destroy(&channel->lock);
    pthread_cond_destroy(&channel->cond);
}

static JanetThreadShared *janet_shared_create(size_t initialSize) {
    const char *errmsg = "could not allocate memory for thread";
    JanetThreadShared *shared = malloc(sizeof(JanetThreadShared));
    if (NULL == shared) janet_panicf(errmsg);
    uint8_t *mem = malloc(initialSize);
    if (NULL == mem) janet_panic(errmsg);
    janet_channel_init(&shared->parent, 0);
    janet_channel_init(&shared->child, initialSize);
    shared->refCount = 2;
    pthread_mutex_init(&shared->refCountLock, NULL);
    return shared;
}

static void janet_shared_destroy(JanetThreadShared *shared) {
    janet_channel_destroy(&shared->parent);
    janet_channel_destroy(&shared->child);
    pthread_mutex_destroy(&shared->refCountLock);
    free(shared);
}

/* Returns 1 if could not send. Does not block or panic. Bytes should be a janet value that
 * has been marshalled. */
static int janet_channel_send_any(JanetChannel *channel, Janet msg, JanetByteView bytes, int is_bytes, JanetTable *dict) {
    pthread_mutex_lock(&channel->lock);

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
        if (is_bytes) {
            janet_buffer_push_bytes(&channel->buf, bytes.bytes, bytes.len);
        } else {
            janet_marshal(&channel->buf, msg, dict, 0);
        }

        /* Was empty, signal to cond */
        if (oldcount == 0) {
            pthread_cond_signal(&channel->cond);
        }
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;
    pthread_mutex_unlock(&channel->lock);

    return ret;
}

static int janet_channel_send(JanetChannel *channel, Janet msg, JanetTable *dict) {
    JanetByteView dud = {0};
    return janet_channel_send_any(channel, msg, dud, 0, dict);
}

/*
static int janet_channel_send_image(JanetChannel *channel, JanetByteView bytes) {
    return janet_channel_send_any(channel, janet_wrap_nil(), bytes, 1, NULL);
}
*/

/* Returns 1 if nothing in queue or failed to get item. Does not block or panic. Uses dict to read bytes from
 * the channel and unmarshal them. */
static int janet_channel_receive(JanetChannel *channel, Janet *msg_out, JanetTable *dict) {
    pthread_mutex_lock(&channel->lock);

    /* If queue is empty, block for now. */
    while (channel->buf.count == 0) {
        pthread_cond_wait(&channel->cond, &channel->lock);
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

static int thread_gc(void *p, size_t size) {
    JanetThread *thread = (JanetThread *)p;
    JanetThreadShared *shared = thread->shared;
    if (NULL == shared) return 0;
    (void) size;
    pthread_mutex_lock(&shared->refCountLock);
    int refcount = --shared->refCount;
    if (refcount == 0) {
        janet_shared_destroy(shared);
    } else {
        pthread_mutex_unlock(&shared->refCountLock);
    }
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

static JanetAbstractType Thread_AT = {
    "core/thread",
    thread_gc,
    thread_mark,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static JanetThread *janet_make_thread(JanetThreadShared *shared, JanetTable *encode, JanetTable *decode, int who) {
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    thread->shared = shared;
    thread->kind = who;
    thread->encode = encode;
    thread->decode = decode;
    return thread;
}

JanetThread *janet_getthread(Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &Thread_AT);
}

/* Runs in new thread */
static int thread_worker(JanetThreadShared *shared) {
    pthread_t handle = pthread_self();
    pthread_detach(handle);

    /* Init VM */
    janet_init();

    /* Get dictionaries */
    JanetTable *decode = janet_get_core_table("load-image-dict");
    JanetTable *encode = janet_get_core_table("make-image-dict");

    /* Create self thread */
    JanetThread *thread = janet_make_thread(shared, encode, decode, JANET_THREAD_SELF);
    Janet threadv = janet_wrap_abstract(thread);

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_channel_receive(&shared->child, &funcv, decode);
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
    thread_worker((JanetThreadShared *)param);
    return NULL;
}

static void janet_thread_start_child(JanetThread *thread) {
    JanetThreadShared *shared = thread->shared;
    pthread_t handle;
    int error = pthread_create(&handle, NULL, janet_pthread_wrapper, shared);
    if (error) {
        thread->shared = NULL; /* Prevent GC from trying to mess with shared memory here */
        janet_shared_destroy(shared);
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
    JanetThreadShared *shared = janet_shared_create(0);
    JanetThread *thread = janet_make_thread(shared, encode, decode, JANET_THREAD_OTHER);
    janet_thread_start_child(thread);
    return janet_wrap_abstract(thread);
}

static Janet cfun_thread_send(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetThread *thread = janet_getthread(argv, 0);
    JanetThreadShared *shared = thread->shared;
    if (NULL == shared) janet_panic("channel has closed");
    int status = janet_channel_send(thread->kind == JANET_THREAD_SELF ? &shared->parent : &shared->child,
                                    argv[1],
                                    thread->encode);
    if (status) {
        janet_panicf("failed to send message %v", argv[1]);
    }
    return argv[0];
}

static Janet cfun_thread_receive(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetThread *thread = janet_getthread(argv, 0);
    JanetThreadShared *shared = thread->shared;
    if (NULL == shared) janet_panic("channel has closed");
    Janet out = janet_wrap_nil();
    int status = janet_channel_receive(thread->kind == JANET_THREAD_SELF ? &shared->child : &shared->parent,
                                       &out,
                                       thread->decode);
    if (status) {
        janet_panic("failed to receive message");
    }
    return out;
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
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_thread(JanetTable *env) {
    janet_core_cfuns(env, NULL, threadlib_cfuns);
}

#endif
