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

#include <math.h>
#ifdef JANET_WINDOWS
#include <windows.h>
#else
#include <setjmp.h>
#include <time.h>
#include <pthread.h>
#endif

/* typedefed in janet.h */
struct JanetMailbox {

    /* Synchronization */
#ifdef JANET_WINDOWS
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
#else
    pthread_mutex_t lock;
    pthread_cond_t cond;
#endif

    /* Receiving messages - (only by owner thread)  */
    JanetTable *decode;

    /* Setup procedure - requires a parent mailbox
     * to receive thunk from */
    JanetMailbox *parent;

    /* Memory management - reference counting */
    int refCount;
    int closed;

    /* Store messages */
    uint16_t messageCapacity;
    uint16_t messageCount;
    uint16_t messageFirst;
    uint16_t messageNext;

    /* Buffers to store messages. These buffers are manually allocated, so
     * are not owned by any thread's GC. */
    JanetBuffer messages[];
};

static JANET_THREAD_LOCAL JanetMailbox *janet_vm_mailbox = NULL;
static JANET_THREAD_LOCAL JanetThread *janet_vm_thread_current = NULL;

static JanetMailbox *janet_mailbox_create(JanetMailbox *parent, int refCount, uint16_t capacity) {
    JanetMailbox *mailbox = malloc(sizeof(JanetMailbox) + sizeof(JanetBuffer) * capacity);
    if (NULL == mailbox) {
        JANET_OUT_OF_MEMORY;
    }
#ifdef JANET_WINDOWS
    InitializeCriticalSection(&mailbox->lock);
    InitializeConditionVariable(&mailbox->cond);
#else
    pthread_mutex_init(&mailbox->lock, NULL);
    pthread_cond_init(&mailbox->cond, NULL);
#endif
    mailbox->refCount = refCount;
    mailbox->closed = 0;
    mailbox->parent = parent;
    mailbox->messageCount = 0;
    mailbox->messageCapacity = capacity;
    mailbox->messageFirst = 0;
    mailbox->messageNext = 0;
    for (uint16_t i = 0; i < capacity; i++) {
        janet_buffer_init(mailbox->messages + i, 0);
    }
    return mailbox;
}

static void janet_mailbox_destroy(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    DeleteCriticalSection(&mailbox->lock);
#else
    pthread_mutex_destroy(&mailbox->lock);
    pthread_cond_destroy(&mailbox->cond);
#endif
    for (uint16_t i = 0; i < mailbox->messageCapacity; i++) {
        janet_buffer_deinit(mailbox->messages + i);
    }
    free(mailbox);
}

static void janet_mailbox_lock(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    EnterCriticalSection(&mailbox->lock);
#else
    pthread_mutex_lock(&mailbox->lock);
#endif
}

static void janet_mailbox_unlock(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    LeaveCriticalSection(&mailbox->lock);
#else
    pthread_mutex_unlock(&mailbox->lock);
#endif
}

/* Assumes you have the mailbox lock already */
static void janet_mailbox_ref_with_lock(JanetMailbox *mailbox, int delta) {
    mailbox->refCount += delta;
    if (mailbox->refCount <= 0) {
        janet_mailbox_unlock(mailbox);
        janet_mailbox_destroy(mailbox);
    } else {
        janet_mailbox_unlock(mailbox);
    }
}

static void janet_mailbox_ref(JanetMailbox *mailbox, int delta) {
    janet_mailbox_lock(mailbox);
    janet_mailbox_ref_with_lock(mailbox, delta);
}

static void janet_close_thread(JanetThread *thread) {
    if (thread->mailbox) {
        janet_mailbox_ref(thread->mailbox, -1);
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
    (void) size;
    JanetThread *thread = (JanetThread *)p;
    if (thread->encode) {
        janet_mark(janet_wrap_table(thread->encode));
    }
    return 0;
}

/* Abstract waiting for timeout across windows/posix */
typedef struct {
    int timedwait;
    int nowait;
#ifdef JANET_WINDOWS
    DWORD interval;
    DWORD ticksLeft;
#else
    struct timespec ts;
#endif
} JanetWaiter;

static void janet_waiter_init(JanetWaiter *waiter, double sec) {
    waiter->timedwait = 0;
    waiter->nowait = 0;

    if (sec == 0.0 || isnan(sec)) {
        waiter->nowait = 1;
        return;
    }
    waiter->timedwait = sec > 0.0;

    /* Set maximum wait time to 30 days */
    if (sec > (60.0 * 60.0 * 24.0 * 30.0)) {
        sec = 60.0 * 60.0 * 24.0 * 30.0;
    }

#ifdef JANET_WINDOWS
    if (waiter->timedwait) {
        waiter->ticksLeft = waiter->interval = (DWORD) floor(1000.0 * sec);
    }
#else
    if (waiter->timedwait) {
        /* N seconds -> timespec of (now + sec) */
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
        waiter->ts.tv_sec = tvsec;
        waiter->ts.tv_nsec = tvnsec;
    }
#endif
}

static int janet_waiter_wait(JanetWaiter *wait, JanetMailbox *mailbox) {
    if (wait->nowait) return 1;
#ifdef JANET_WINDOWS
    if (wait->timedwait) {
        if (wait->ticksLeft == 0) return 1;
        DWORD startTime = GetTickCount();
        int status = !SleepConditionVariableCS(&mailbox->cond, &mailbox->lock, wait->ticksLeft);
        DWORD dTick = GetTickCount() - startTime;
        /* Be careful about underflow */
        wait->ticksLeft = dTick > wait->ticksLeft ? 0 : dTick;
        return status;
    } else {
        SleepConditionVariableCS(&mailbox->cond, &mailbox->lock, INFINITE);
        return 0;
    }
#else
    if (wait->timedwait) {
        return pthread_cond_timedwait(&mailbox->cond, &mailbox->lock, &wait->ts);
    } else {
        pthread_cond_wait(&mailbox->cond, &mailbox->lock);
        return 0;
    }
#endif
}

static void janet_mailbox_wakeup(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    WakeConditionVariable(&mailbox->cond);
#else
    pthread_cond_signal(&mailbox->cond);
#endif
}

static int mailbox_at_capacity(JanetMailbox *mailbox) {
    return mailbox->messageCount >= mailbox->messageCapacity;
}

/* Returns 1 if could not send (encode error or timeout), 2 for mailbox closed, and
 * 0 otherwise. Will not panic.  */
int janet_thread_send(JanetThread *thread, Janet msg, double timeout) {

    /* Ensure mailbox is not closed. */
    JanetMailbox *mailbox = thread->mailbox;
    if (NULL == mailbox) return 2;
    janet_mailbox_lock(mailbox);
    if (mailbox->closed) {
        janet_mailbox_ref_with_lock(mailbox, -1);
        thread->mailbox = NULL;
        return 2;
    }

    /* Back pressure */
    if (mailbox_at_capacity(mailbox)) {
        JanetWaiter wait;
        janet_waiter_init(&wait, timeout);

        if (wait.nowait) {
            janet_mailbox_unlock(mailbox);
            return 1;
        }

        /* Retry loop, as there can be multiple writers */
        while (mailbox_at_capacity(mailbox)) {
            if (janet_waiter_wait(&wait, mailbox)) {
                janet_mailbox_unlock(mailbox);
                janet_mailbox_wakeup(mailbox);
                return 1;
            }
        }
    }

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;
    int32_t oldmcount = mailbox->messageCount;

    int ret = 0;
    if (setjmp(buf)) {
        ret = 1;
        mailbox->messageCount = oldmcount;
    } else {
        JanetBuffer *msgbuf = mailbox->messages + mailbox->messageNext;
        msgbuf->count = 0;

        /* Start panic zone */
        janet_marshal(msgbuf, msg, thread->encode, 0);
        /* End panic zone */

        mailbox->messageNext = (mailbox->messageNext + 1) % mailbox->messageCapacity;
        mailbox->messageCount++;
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;
    janet_mailbox_unlock(mailbox);

    /* Potentially wake up a blocked thread */
    janet_mailbox_wakeup(mailbox);

    return ret;
}

/* Returns 0 on successful message. Returns 1 if timedout */
int janet_thread_receive(Janet *msg_out, double timeout) {
    JanetMailbox *mailbox = janet_vm_mailbox;
    janet_mailbox_lock(mailbox);

    /* For timeouts */
    JanetWaiter wait;
    janet_waiter_init(&wait, timeout);

    for (;;) {

        /* Check for messages waiting for us */
        if (mailbox->messageCount > 0) {

            /* Hack to capture all panics from marshalling. This works because
             * we know janet_marshal won't mess with other essential global state. */
            jmp_buf buf;
            jmp_buf *old_buf = janet_vm_jmp_buf;
            janet_vm_jmp_buf = &buf;

            /* Handle errors */
            if (setjmp(buf)) {
                /* Cleanup jmp_buf, keep lock */
                janet_vm_jmp_buf = old_buf;
            } else {
                JanetBuffer *msgbuf = mailbox->messages + mailbox->messageFirst;
                mailbox->messageCount--;
                mailbox->messageFirst = (mailbox->messageFirst + 1) % mailbox->messageCapacity;

                /* Read from beginning of channel */
                const uint8_t *nextItem = NULL;
                Janet item = janet_unmarshal(
                                 msgbuf->data, msgbuf->count,
                                 0, mailbox->decode, &nextItem);
                *msg_out = item;

                /* Cleanup */
                janet_vm_jmp_buf = old_buf;
                janet_mailbox_unlock(mailbox);

                /* Potentially wake up pending threads */
                janet_mailbox_wakeup(mailbox);

                return 0;
            }
        }

        if (wait.nowait || mailbox->refCount <= 1) {
            janet_mailbox_unlock(mailbox);
            return 1;
        }

        /* Wait for next message */
        if (janet_waiter_wait(&wait, mailbox)) {
            janet_mailbox_unlock(mailbox);
            return 1;
        }
    }

}

static int janet_thread_getter(void *p, Janet key, Janet *out);

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

static JanetThread *janet_make_thread(JanetMailbox *mailbox, JanetTable *encode) {
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    thread->mailbox = mailbox;
    thread->encode = encode;
    return thread;
}

JanetThread *janet_getthread(const Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &Thread_AT);
}

static JanetTable *janet_get_core_table(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    JanetBindingType bt = janet_resolve(env, janet_csymbol(name), &out);
    if (bt == JANET_BINDING_NONE) return NULL;
    if (!janet_checktype(out, JANET_TABLE)) return NULL;
    return janet_unwrap_table(out);
}

/* Runs in new thread */
static int thread_worker(JanetMailbox *mailbox) {
    JanetFiber *fiber = NULL;
    Janet out;

    /* Use the mailbox we were given */
    janet_vm_mailbox = mailbox;

    /* Init VM */
    janet_init();

    /* Get dictionaries for default encode/decode */
    JanetTable *encode = janet_get_core_table("make-image-dict");
    mailbox->decode = janet_get_core_table("load-image-dict");

    /* Create parent thread */
    JanetThread *parent = janet_make_thread(mailbox->parent, encode);
    janet_mailbox_ref(mailbox->parent, -1);
    mailbox->parent = NULL; /* only used to create the thread */
    Janet parentv = janet_wrap_abstract(parent);

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_thread_receive(&funcv, -1.0);

    if (status) goto error;
    if (!janet_checktype(funcv, JANET_FUNCTION)) goto error;
    JanetFunction *func = janet_unwrap_function(funcv);

    /* Arity check */
    if (func->def->min_arity > 1 || func->def->max_arity < 1) {
        goto error;
    }

    /* Call function */
    Janet argv[1] = { parentv };
    fiber = janet_fiber(func, 64, 1, argv);
    JanetSignal sig = janet_continue(fiber, janet_wrap_nil(), &out);
    if (sig != JANET_SIGNAL_OK) {
        janet_eprintf("in thread %v: ", janet_wrap_abstract(janet_make_thread(mailbox, encode)));
        janet_stacktrace(fiber, out);
    }

    /* Normal exit */
    janet_deinit();
    return 0;

    /* Fail to set something up */
error:
    janet_eprintf("\nthread failed to start\n");
    janet_deinit();
    return 1;
}

#ifdef JANET_WINDOWS

static DWORD WINAPI janet_create_thread_wrapper(LPVOID param) {
    thread_worker((JanetMailbox *)param);
    return 0;
}

static int janet_thread_start_child(JanetThread *thread) {
    HANDLE handle = CreateThread(NULL, 0, janet_create_thread_wrapper, thread->mailbox, 0, NULL);
    int ret = NULL == handle;
    /* Does not kill thread, simply detatches */
    if (!ret) CloseHandle(handle);
    return ret;
}

#else

static void *janet_pthread_wrapper(void *param) {
    thread_worker((JanetMailbox *)param);
    return NULL;
}

static int janet_thread_start_child(JanetThread *thread) {
    pthread_t handle;
    int error = pthread_create(&handle, NULL, janet_pthread_wrapper, thread->mailbox);
    if (error) {
        return 1;
    } else {
        pthread_detach(handle);
        return 0;
    }
}

#endif

/*
 * Setup/Teardown
 */

void janet_threads_init(void) {
    if (NULL == janet_vm_mailbox) {
        janet_vm_mailbox = janet_mailbox_create(NULL, 1, 10);
    }
}

void janet_threads_deinit(void) {
    janet_mailbox_lock(janet_vm_mailbox);
    janet_vm_mailbox->closed = 1;
    janet_mailbox_ref_with_lock(janet_vm_mailbox, -1);
    janet_vm_mailbox = NULL;
    janet_vm_thread_current = NULL;
}

/*
 * Cfuns
 */

static Janet cfun_thread_current(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    if (NULL == janet_vm_thread_current) {
        janet_vm_thread_current = janet_make_thread(janet_vm_mailbox, janet_get_core_table("make-image-dict"));
        janet_mailbox_ref(janet_vm_mailbox, 1);
        janet_gcroot(janet_wrap_abstract(janet_vm_thread_current));
    }
    return janet_wrap_abstract(janet_vm_thread_current);
}

static Janet cfun_thread_new(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    int32_t cap = janet_optinteger(argv, argc, 0, 10);
    if (cap < 1 || cap > UINT16_MAX) {
        janet_panicf("bad slot #1, expected integer in range [1, 65535], got %d", cap);
    }
    JanetTable *encode = janet_get_core_table("make-image-dict");
    JanetMailbox *mailbox = janet_mailbox_create(janet_vm_mailbox, 2, (uint16_t) cap);

    /* one for created thread, one for ->parent reference in new mailbox */
    janet_mailbox_ref(janet_vm_mailbox, 2);

    JanetThread *thread = janet_make_thread(mailbox, encode);
    if (janet_thread_start_child(thread)) {
        janet_mailbox_ref(mailbox, -1); /* mailbox reference */
        janet_mailbox_ref(janet_vm_mailbox, -1); /* ->parent reference */
        janet_panic("could not start thread");
    }
    return janet_wrap_abstract(thread);
}

static Janet cfun_thread_send(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetThread *thread = janet_getthread(argv, 0);
    int status = janet_thread_send(thread, argv[1], janet_optnumber(argv, argc, 2, 1.0));
    switch (status) {
        default:
            break;
        case 1:
            janet_panicf("failed to send message %v", argv[1]);
        case 2:
            janet_panic("thread mailbox is closed");
    }
    return argv[0];
}

static Janet cfun_thread_receive(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    double wait = janet_optnumber(argv, argc, 0, 1.0);
    Janet out;
    int status = janet_thread_receive(&out, wait);
    switch (status) {
        default:
            break;
        case 1:
            janet_panicf("timeout after %f seconds", wait);
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
    {"close", cfun_thread_close},
    {NULL, NULL}
};

static int janet_thread_getter(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), janet_thread_methods, out);
}

static const JanetReg threadlib_cfuns[] = {
    {
        "thread/current", cfun_thread_current,
        JDOC("(thread/current)\n\n"
             "Get the current running thread.")
    },
    {
        "thread/new", cfun_thread_new,
        JDOC("(thread/new &opt capacity)\n\n"
             "Start a new thread. The thread will wait for a message containing the function used to start the thread, which should be passed to the thread "
             "via thread/send. If capacity is provided, that is how many messages can be stored in the thread's mailbox before blocking senders. "
             "The capacity must be between 1 and 65535 inclusive, and defaults to 10. "
             "Returns a handle to the new thread.")
    },
    {
        "thread/send", cfun_thread_send,
        JDOC("(thread/send thread msg)\n\n"
             "Send a message to the thread. This will never block and returns thread immediately. "
             "Will throw an error if there is a problem sending the message.")
    },
    {
        "thread/receive", cfun_thread_receive,
        JDOC("(thread/receive &opt timeout)\n\n"
             "Get a message sent to this thread. If timeout is provided, an error will be thrown after the timeout has elapsed but "
             "no messages are received.")
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
    janet_register_abstract_type(&Thread_AT);
}

#endif
