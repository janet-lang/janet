/*
* Copyright (c) 2024 Calvin Rose
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
#include "gc.h"
#include "state.h"
#endif

#ifdef JANET_EV
#ifdef JANET_WINDOWS
#include <windows.h>
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

size_t janet_os_mutex_size(void) {
    return sizeof(CRITICAL_SECTION);
}

size_t janet_os_rwlock_size(void) {
    return sizeof(void *);
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
    /* error handling? May want to keep counter */
    LeaveCriticalSection((CRITICAL_SECTION *) mutex);
}

void janet_os_rwlock_init(JanetOSRWLock *rwlock) {
    InitializeSRWLock((PSRWLOCK) rwlock);
}

void janet_os_rwlock_deinit(JanetOSRWLock *rwlock) {
    /* no op? */
    (void) rwlock;
}

void janet_os_rwlock_rlock(JanetOSRWLock *rwlock) {
    AcquireSRWLockShared((PSRWLOCK) rwlock);
}

void janet_os_rwlock_wlock(JanetOSRWLock *rwlock) {
    AcquireSRWLockExclusive((PSRWLOCK) rwlock);
}

void janet_os_rwlock_runlock(JanetOSRWLock *rwlock) {
    ReleaseSRWLockShared((PSRWLOCK) rwlock);
}

void janet_os_rwlock_wunlock(JanetOSRWLock *rwlock) {
    ReleaseSRWLockExclusive((PSRWLOCK) rwlock);
}

#else

size_t janet_os_mutex_size(void) {
    return sizeof(pthread_mutex_t);
}

size_t janet_os_rwlock_size(void) {
    return sizeof(pthread_rwlock_t);
}

void janet_os_mutex_init(JanetOSMutex *mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init((pthread_mutex_t *) mutex, &attr);
}

void janet_os_mutex_deinit(JanetOSMutex *mutex) {
    pthread_mutex_destroy((pthread_mutex_t *) mutex);
}

void janet_os_mutex_lock(JanetOSMutex *mutex) {
    pthread_mutex_lock((pthread_mutex_t *) mutex);
}

void janet_os_mutex_unlock(JanetOSMutex *mutex) {
    int ret = pthread_mutex_unlock((pthread_mutex_t *) mutex);
    if (ret) janet_panic("cannot release lock");
}

void janet_os_rwlock_init(JanetOSRWLock *rwlock) {
    pthread_rwlock_init((pthread_rwlock_t *) rwlock, NULL);
}

void janet_os_rwlock_deinit(JanetOSRWLock *rwlock) {
    pthread_rwlock_destroy((pthread_rwlock_t *) rwlock);
}

void janet_os_rwlock_rlock(JanetOSRWLock *rwlock) {
    pthread_rwlock_rdlock((pthread_rwlock_t *) rwlock);
}

void janet_os_rwlock_wlock(JanetOSRWLock *rwlock) {
    pthread_rwlock_wrlock((pthread_rwlock_t *) rwlock);
}

void janet_os_rwlock_runlock(JanetOSRWLock *rwlock) {
    pthread_rwlock_unlock((pthread_rwlock_t *) rwlock);
}

void janet_os_rwlock_wunlock(JanetOSRWLock *rwlock) {
    pthread_rwlock_unlock((pthread_rwlock_t *) rwlock);
}

#endif

int32_t janet_abstract_incref(void *abst) {
    return janet_atomic_inc(&janet_abstract_head(abst)->gc.data.refcount);
}

int32_t janet_abstract_decref(void *abst) {
    return janet_atomic_dec(&janet_abstract_head(abst)->gc.data.refcount);
}

#endif
