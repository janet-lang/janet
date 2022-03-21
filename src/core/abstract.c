/*
* Copyright (c) 2022 Calvin Rose
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
#include "gc.h"
#include "state.h"
#ifdef JANET_EV
#ifdef JANET_WINDOWS
#include <windows.h>
#endif
#endif
#endif

/* Create new userdata */
void *janet_abstract_begin(const JanetAbstractType *atype, size_t size) {
    JanetAbstractHead *header = janet_gcalloc(JANET_MEMORY_NONE,
                                sizeof(JanetAbstractHead) + size);
    header->size = size;
    header->type = atype;
    return (void *) & (header->data);
}

void *janet_abstract_end(void *x) {
    janet_gc_settype((void *)(janet_abstract_head(x)), JANET_MEMORY_ABSTRACT);
    return x;
}

void *janet_abstract(const JanetAbstractType *atype, size_t size) {
    return janet_abstract_end(janet_abstract_begin(atype, size));
}

#ifdef JANET_EV

/*
 * Threaded abstracts
 */

void *janet_abstract_begin_threaded(const JanetAbstractType *atype, size_t size) {
    JanetAbstractHead *header = janet_malloc(sizeof(JanetAbstractHead) + size);
    if (NULL == header) {
        JANET_OUT_OF_MEMORY;
    }
    janet_vm.next_collection += size + sizeof(JanetAbstractHead);
    header->gc.flags = JANET_MEMORY_THREADED_ABSTRACT;
    header->gc.data.next = NULL; /* Clear memory for address sanitizers */
    header->gc.data.refcount = 1;
    header->size = size;
    header->type = atype;
    void *abstract = (void *) & (header->data);
    janet_table_put(&janet_vm.threaded_abstracts, janet_wrap_abstract(abstract), janet_wrap_false());
    return abstract;
}

void *janet_abstract_end_threaded(void *x) {
    janet_gc_settype((void *)(janet_abstract_head(x)), JANET_MEMORY_THREADED_ABSTRACT);
    return x;
}

void *janet_abstract_threaded(const JanetAbstractType *atype, size_t size) {
    return janet_abstract_end_threaded(janet_abstract_begin_threaded(atype, size));
}

/* Refcounting primitives and sync primitives */

#ifdef JANET_WINDOWS

static int32_t janet_incref(JanetAbstractHead *ab) {
    return InterlockedIncrement(&ab->gc.data.refcount);
}

static int32_t janet_decref(JanetAbstractHead *ab) {
    return InterlockedDecrement(&ab->gc.data.refcount);
}

void janet_os_mutex_init(JanetOSMutex *mutex) {
    InitializeCriticalSection((CRITICAL_SECTION *) mutex);
}

void janet_os_mutex_deinit(JanetOSMutex *mutex) {
    DeleteCriticalSection((CRITICAL_SECTION *) mutex);
}

void janet_os_mutex_lock(JanetOSMutex *mutex) {
    EnterCriticalSection((CRITICAL_SECTION *) mutex);
}

void janet_os_mutex_unlock(JanetOSMutex *mutex) {
    LeaveCriticalSection((CRITICAL_SECTION *) mutex);
}

#else

static int32_t janet_incref(JanetAbstractHead *ab) {
    return __atomic_add_fetch(&ab->gc.data.refcount, 1, __ATOMIC_RELAXED);
}

static int32_t janet_decref(JanetAbstractHead *ab) {
    return __atomic_add_fetch(&ab->gc.data.refcount, -1, __ATOMIC_RELAXED);
}

void janet_os_mutex_init(JanetOSMutex *mutex) {
    pthread_mutex_init(mutex, NULL);
}

void janet_os_mutex_deinit(JanetOSMutex *mutex) {
    pthread_mutex_destroy(mutex);
}

void janet_os_mutex_lock(JanetOSMutex *mutex) {
    pthread_mutex_lock(mutex);
}

void janet_os_mutex_unlock(JanetOSMutex *mutex) {
    pthread_mutex_unlock(mutex);
}

#endif

int32_t janet_abstract_incref(void *abst) {
    return janet_incref(janet_abstract_head(abst));
}

int32_t janet_abstract_decref(void *abst) {
    return janet_decref(janet_abstract_head(abst));
}

#endif
