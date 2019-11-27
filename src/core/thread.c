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
#endif

#ifdef JANET_THREADS

#include <pthread.h>

static void shared_cleanup(JanetThreadShared *shared) {
    pthread_mutex_destroy(&shared->refCountLock);
    pthread_mutex_destroy(&shared->memoryLock);
    free(shared->memory);
    free(shared);
}

static int thread_gc(void *p, size_t size) {
    JanetThread *thread = (JanetThread *)p;
    JanetThreadShared *shared = thread->shared;
    if (NULL == shared) return 0;
    (void) size;
    pthread_mutex_lock(&shared->refCountLock);
    int refcount = --shared->refCount;
    if (refcount == 0) {
        shared_cleanup(shared);
    } else {
        pthread_mutex_unlock(&shared->refCountLock);
    }
    return 0;
}

static JanetAbstractType Thread_AT = {
    "core/thread",
    thread_gc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

JanetThread *janet_getthread(Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &Thread_AT);
}

/* Runs in new thread */
static int thread_worker(JanetThreadShared *shared) {
    /* Init VM */
    janet_init();

    JanetTable *dict = janet_core_dictionary(NULL);
    const uint8_t *next = NULL;

    /* Unmarshal the function */
    Janet funcv = janet_unmarshal(shared->memory, shared->memorySize, 0, dict, &next);
    if (next == shared->memory) goto error;
    if (!janet_checktype(funcv, JANET_FUNCTION)) goto error;
    JanetFunction *func = janet_unwrap_function(funcv);

    /* Create self thread */
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    thread->shared = shared;
    thread->handle = pthread_self();

    /* Clean up thread when done, do not wait for a join. For
     * communicating with other threads, we will rely on the
     * JanetThreadShared structure. */
    pthread_detach(thread->handle);

    /* Call function */
    JanetFiber *fiber = janet_fiber(func, 64, 0, NULL);
    fiber->env = janet_table(0);
    janet_table_put(fiber->env, janet_ckeywordv("worker"), janet_wrap_abstract(thread));
    Janet out;
    janet_continue(fiber, janet_wrap_nil(), &out);

    /* TODO - marshal 'out' into sharedMemory */

    /* Success */
    janet_deinit();
    return 0;

    /* Fail */
error:
    janet_deinit();
    return 1;
}

void *janet_pthread_wrapper(void *param) {
    thread_worker((JanetThreadShared *)param);
    return NULL;
}

static Janet cfun_from_image(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetByteView bytes = janet_getbytes(argv, 0);

    /* Create Shared memory chunk of thread object */
    JanetThreadShared *shared = malloc(sizeof(JanetThreadShared));
    uint8_t *mem = malloc(bytes.len);
    if (NULL == shared || NULL == mem) {
        janet_panicf("could not allocate memory for thread");
    }
    shared->memory = mem;
    shared->memorySize = bytes.len;
    memcpy(mem, bytes.bytes, bytes.len);
    shared->refCount = 2;
    pthread_mutex_init(&shared->refCountLock, NULL);
    pthread_mutex_init(&shared->memoryLock, NULL);

    /* Create thread abstract */
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    thread->shared = shared;

    /* Run thread */
    int error = pthread_create(&thread->handle, NULL, janet_pthread_wrapper, shared);
    if (error) {
        thread->shared = NULL; /* Prevent GC from trying to mess with shared memory here */
        shared_cleanup(shared);
    }

    return janet_wrap_abstract(thread);
}

static const JanetReg it_cfuns[] = {
    {
        "thread/from-image", cfun_from_image,
        JDOC("(thread/from-image image)\n\n"
             "Start a new thread. image is a byte sequence, containing a marshalled function.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_thread(JanetTable *env) {
    janet_core_cfuns(env, NULL, it_cfuns);
}

#endif
