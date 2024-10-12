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
#endif

#ifdef JANET_EV
#ifdef JANET_FILEWATCH

#ifdef JANET_LINUX
#include <sys/inotify.h>
#include <unistd.h>
#endif

#ifdef JANET_WINDOWS
#include <windows.h>
#endif

typedef struct {
    const char *name;
    uint32_t flag;
} JanetWatchFlagName;

typedef struct {
#ifndef JANET_WINDOWS
    JanetStream *stream;
#endif
    JanetTable *watch_descriptors;
    JanetChannel *channel;
    uint32_t default_flags;
    int is_watching;
} JanetWatcher;

#ifdef JANET_LINUX

#include <sys/inotify.h>
#include <unistd.h>

static const JanetWatchFlagName watcher_flags_linux[] = {
    {"access", IN_ACCESS},
    {"all", IN_ALL_EVENTS},
    {"attrib", IN_ATTRIB},
    {"close-nowrite", IN_CLOSE_NOWRITE},
    {"close-write", IN_CLOSE_WRITE},
    {"create", IN_CREATE},
    {"delete", IN_DELETE},
    {"delete-self", IN_DELETE_SELF},
    {"ignored", IN_IGNORED},
    {"modify", IN_MODIFY},
    {"move-self", IN_MOVE_SELF},
    {"moved-from", IN_MOVED_FROM},
    {"moved-to", IN_MOVED_TO},
    {"open", IN_OPEN},
    {"q-overflow", IN_Q_OVERFLOW},
    {"unmount", IN_UNMOUNT},
};

static uint32_t decode_watch_flags(const Janet *options, int32_t n) {
    uint32_t flags = 0;
    for (int32_t i = 0; i < n; i++) {
        if (!(janet_checktype(options[i], JANET_KEYWORD))) {
            janet_panicf("expected keyword, got %v", options[i]);
        }
        JanetKeyword keyw = janet_unwrap_keyword(options[i]);
        const JanetWatchFlagName *result = janet_strbinsearch(watcher_flags_linux,
                                           sizeof(watcher_flags_linux) / sizeof(JanetWatchFlagName),
                                           sizeof(JanetWatchFlagName),
                                           keyw);
        if (!result) {
            janet_panicf("unknown inotify flag %v", options[i]);
        }
        flags |= result->flag;
    }
    return flags;
}

static void janet_watcher_init(JanetWatcher *watcher, JanetChannel *channel, uint32_t default_flags) {
    int fd;
    do {
        fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) {
        janet_panicv(janet_ev_lasterr());
    }
    watcher->watch_descriptors = janet_table(0);
    watcher->channel = channel;
    watcher->default_flags = default_flags;
    watcher->is_watching = 0;
    watcher->stream = janet_stream(fd, JANET_STREAM_READABLE, NULL);
}

static void janet_watcher_add(JanetWatcher *watcher, const char *path, uint32_t flags) {
    if (watcher->stream == NULL) janet_panic("watcher closed");
    int result;
    do {
        result = inotify_add_watch(watcher->stream->handle, path, flags);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        janet_panicv(janet_ev_lasterr());
    }
    Janet name = janet_cstringv(path);
    Janet wd = janet_wrap_integer(result);
    janet_table_put(watcher->watch_descriptors, name, wd);
    janet_table_put(watcher->watch_descriptors, wd, name);
}

static void janet_watcher_remove(JanetWatcher *watcher, const char *path) {
    if (watcher->stream == NULL) janet_panic("watcher closed");
    Janet check = janet_table_get(watcher->watch_descriptors, janet_cstringv(path));
    janet_assert(janet_checktype(check, JANET_NUMBER), "bad watch descriptor");
    int watch_handle = janet_unwrap_integer(check);
    int result;
    do {
        result = inotify_rm_watch(watcher->stream->handle, watch_handle);
    } while (result != -1 && errno == EINTR);
    if (result == -1) {
        janet_panicv(janet_ev_lasterr());
    }
}

static void watcher_callback_read(JanetFiber *fiber, JanetAsyncEvent event) {
    JanetStream *stream = fiber->ev_stream;
    JanetWatcher *watcher = *((JanetWatcher **) fiber->ev_state);
    char buf[1024];
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(janet_wrap_abstract(watcher));
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_schedule(fiber, janet_wrap_nil());
            janet_async_end(fiber);
            break;
        case JANET_ASYNC_EVENT_ERR: {
            janet_schedule(fiber, janet_wrap_nil());
            janet_async_end(fiber);
            break;
        }
    read_more:
        case JANET_ASYNC_EVENT_HUP:
        case JANET_ASYNC_EVENT_INIT:
        case JANET_ASYNC_EVENT_READ: {
            Janet name = janet_wrap_nil();

            /* Assumption - read will never return partial events *
             * From documentation:
             *
             * The behavior when the buffer given to read(2) is too small to
             * return information about the next event depends on the kernel
             * version: before Linux 2.6.21, read(2) returns 0; since Linux
             * 2.6.21, read(2) fails with the error EINVAL.  Specifying a buffer
             * of size
             *
             *     sizeof(struct inotify_event) + NAME_MAX + 1
             *
             * will be sufficient to read at least one event. */
            ssize_t nread;
            do {
                nread = read(stream->handle, buf, sizeof(buf));
            } while (nread == -1 && errno == EINTR);

            /* Check for errors - special case errors that can just be waited on to fix */
            if (nread == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                janet_cancel(fiber, janet_ev_lasterr());
                fiber->ev_state = NULL;
                janet_async_end(fiber);
                break;
            }
            if (nread < (ssize_t) sizeof(struct inotify_event)) break;

            /* Iterate through all events read from the buffer */
            char *cursor = buf;
            while (cursor < buf + nread) {
                struct inotify_event inevent;
                memcpy(&inevent, cursor, sizeof(inevent));
                cursor += sizeof(inevent);
                /* Read path of inevent */
                if (inevent.len) {
                    name = janet_cstringv(cursor);
                    cursor += inevent.len;
                }

                /* Got an event */
                Janet path = janet_table_get(watcher->watch_descriptors, janet_wrap_integer(inevent.wd));
                JanetKV *event = janet_struct_begin(6);
                janet_struct_put(event, janet_ckeywordv("wd"), janet_wrap_integer(inevent.wd));
                janet_struct_put(event, janet_ckeywordv("wd-path"), path);
                if (janet_checktype(name, JANET_NIL)) {
                    /* We were watching a file directly, so path is the full path. Split into dirname / basename */
                    JanetString spath = janet_unwrap_string(path);
                    const uint8_t *cursor = spath + janet_string_length(spath);
                    const uint8_t *cursor_end = cursor;
                    while (cursor > spath && cursor[0] != '/') {
                        cursor--;
                    }
                    if (cursor == spath) {
                        janet_struct_put(event, janet_ckeywordv("dir-name"), path);
                        janet_struct_put(event, janet_ckeywordv("file-name"), name);
                    } else {
                        janet_struct_put(event, janet_ckeywordv("dir-name"), janet_wrap_string(janet_string(spath, (cursor - spath))));
                        janet_struct_put(event, janet_ckeywordv("file-name"), janet_wrap_string(janet_string(cursor + 1, (cursor_end - cursor - 1))));
                    }
                } else {
                    janet_struct_put(event, janet_ckeywordv("dir-name"), path);
                    janet_struct_put(event, janet_ckeywordv("file-name"), name);
                }
                janet_struct_put(event, janet_ckeywordv("cookie"), janet_wrap_integer(inevent.cookie));
                Janet etype = janet_ckeywordv("type");
                const JanetWatchFlagName *wfn_end = watcher_flags_linux + sizeof(watcher_flags_linux) / sizeof(watcher_flags_linux[0]);
                for (const JanetWatchFlagName *wfn = watcher_flags_linux; wfn < wfn_end; wfn++) {
                    if ((inevent.mask & wfn->flag) == wfn->flag) janet_struct_put(event, etype, janet_ckeywordv(wfn->name));
                }
                Janet eventv = janet_wrap_struct(janet_struct_end(event));

                janet_channel_give(watcher->channel, eventv);
            }

            /* Read some more if possible */
            goto read_more;
        }
        break;
    }
}

static void janet_watcher_listen(JanetWatcher *watcher) {
    if (watcher->is_watching) janet_panic("already watching");
    watcher->is_watching = 1;
    JanetFunction *thunk = janet_thunk_delay(janet_wrap_nil());
    JanetFiber *fiber = janet_fiber(thunk, 64, 0, NULL);
    JanetWatcher **state = janet_malloc(sizeof(JanetWatcher *)); /* Gross */
    *state = watcher;
    janet_async_start_fiber(fiber, watcher->stream, JANET_ASYNC_LISTEN_READ, watcher_callback_read, state);
    janet_gcroot(janet_wrap_abstract(watcher));
}

static void janet_watcher_unlisten(JanetWatcher *watcher) {
    if (!watcher->is_watching) return;
    watcher->is_watching = 0;
    janet_stream_close(watcher->stream);
    janet_gcunroot(janet_wrap_abstract(watcher));
}

#elif JANET_WINDOWS

#define WATCHFLAG_RECURSIVE 0x100000u

static const JanetWatchFlagName watcher_flags_windows[] = {
    {
        "all",
        FILE_NOTIFY_CHANGE_ATTRIBUTES |
        FILE_NOTIFY_CHANGE_CREATION |
        FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_LAST_ACCESS |
        FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_SECURITY |
        FILE_NOTIFY_CHANGE_SIZE |
        WATCHFLAG_RECURSIVE
    },
    {"attributes", FILE_NOTIFY_CHANGE_ATTRIBUTES},
    {"creation", FILE_NOTIFY_CHANGE_CREATION},
    {"dir-name", FILE_NOTIFY_CHANGE_DIR_NAME},
    {"file-name", FILE_NOTIFY_CHANGE_FILE_NAME},
    {"last-access", FILE_NOTIFY_CHANGE_LAST_ACCESS},
    {"last-write", FILE_NOTIFY_CHANGE_LAST_WRITE},
    {"recursive", WATCHFLAG_RECURSIVE},
    {"security", FILE_NOTIFY_CHANGE_SECURITY},
    {"size", FILE_NOTIFY_CHANGE_SIZE},
};

static uint32_t decode_watch_flags(const Janet *options, int32_t n) {
    uint32_t flags = 0;
    for (int32_t i = 0; i < n; i++) {
        if (!(janet_checktype(options[i], JANET_KEYWORD))) {
            janet_panicf("expected keyword, got %v", options[i]);
        }
        JanetKeyword keyw = janet_unwrap_keyword(options[i]);
        const JanetWatchFlagName *result = janet_strbinsearch(watcher_flags_windows,
                                           sizeof(watcher_flags_windows) / sizeof(JanetWatchFlagName),
                                           sizeof(JanetWatchFlagName),
                                           keyw);
        if (!result) {
            janet_panicf("unknown windows filewatch flag %v", options[i]);
        }
        flags |= result->flag;
    }
    return flags;
}

static void janet_watcher_init(JanetWatcher *watcher, JanetChannel *channel, uint32_t default_flags) {
    watcher->watch_descriptors = janet_table(0);
    watcher->channel = channel;
    watcher->default_flags = default_flags;
    watcher->is_watching = 0;
}

/* Since the file info padding includes embedded file names, we want to include more space for data.
 * We also need to handle manually calculating changes if path names are too long, but ideally just avoid
 * that scenario as much as possible */
#define FILE_INFO_PADDING (4096 * 4)

typedef struct {
    OVERLAPPED overlapped;
    JanetStream *stream;
    JanetWatcher *watcher;
    JanetFiber *fiber;
    JanetString dir_path;
    uint32_t flags;
    uint64_t buf[FILE_INFO_PADDING / sizeof(uint64_t)]; /* Ensure alignment */
} OverlappedWatch;

#define NotifyChange FILE_NOTIFY_INFORMATION

static void read_dir_changes(OverlappedWatch *ow) {
    BOOL result = ReadDirectoryChangesW(ow->stream->handle,
                                        (NotifyChange *) ow->buf,
                                        FILE_INFO_PADDING,
                                        (ow->flags & WATCHFLAG_RECURSIVE) ? TRUE : FALSE,
                                        ow->flags & ~WATCHFLAG_RECURSIVE,
                                        NULL,
                                        (OVERLAPPED *) ow,
                                        NULL);
    if (!result) {
        janet_panicv(janet_ev_lasterr());
    }
}

static const char *watcher_actions_windows[] = {
    "unknown",
    "added",
    "removed",
    "modified",
    "renamed-old",
    "renamed-new",
};

static void watcher_callback_read(JanetFiber *fiber, JanetAsyncEvent event) {
    OverlappedWatch *ow = (OverlappedWatch *) fiber->ev_state;
    JanetWatcher *watcher = ow->watcher;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_INIT:
            janet_async_in_flight(fiber);
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(janet_wrap_abstract(ow->stream));
            janet_mark(janet_wrap_fiber(ow->fiber));
            janet_mark(janet_wrap_abstract(watcher));
            janet_mark(janet_wrap_string(ow->dir_path));
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_table_remove(ow->watcher->watch_descriptors, janet_wrap_string(ow->dir_path));
            break;
        case JANET_ASYNC_EVENT_ERR:
        case JANET_ASYNC_EVENT_FAILED:
            janet_stream_close(ow->stream);
            break;
        case JANET_ASYNC_EVENT_COMPLETE: {
            if (!watcher->is_watching) {
                janet_stream_close(ow->stream);
                break;
            }

            NotifyChange *fni = (NotifyChange *) ow->buf;

            while (1) {
                /* Got an event */

                /* Extract name */
                Janet filename;
                if (fni->FileNameLength) {
                    int32_t nbytes = (int32_t) WideCharToMultiByte(CP_UTF8, 0, fni->FileName, fni->FileNameLength / sizeof(wchar_t), NULL, 0, NULL, NULL);
                    janet_assert(nbytes, "bad utf8 path");
                    uint8_t *into = janet_string_begin(nbytes);
                    WideCharToMultiByte(CP_UTF8, 0, fni->FileName, fni->FileNameLength / sizeof(wchar_t), (char *) into, nbytes, NULL, NULL);
                    filename = janet_wrap_string(janet_string_end(into));
                } else {
                    filename = janet_cstringv("");
                }

                JanetKV *event = janet_struct_begin(3);
                janet_struct_put(event, janet_ckeywordv("type"), janet_ckeywordv(watcher_actions_windows[fni->Action]));
                janet_struct_put(event, janet_ckeywordv("file-name"), filename);
                janet_struct_put(event, janet_ckeywordv("dir-name"), janet_wrap_string(ow->dir_path));
                Janet eventv = janet_wrap_struct(janet_struct_end(event));

                janet_channel_give(watcher->channel, eventv);

                /* Next event */
                if (!fni->NextEntryOffset) break;
                fni = (NotifyChange *)((char *)fni + fni->NextEntryOffset);
            }

            /* Make another call to read directory changes */
            read_dir_changes(ow);
            janet_async_in_flight(fiber);
        }
        break;
    }
}

static void start_listening_ow(OverlappedWatch *ow) {
    read_dir_changes(ow);
    JanetStream *stream = ow->stream;
    JanetFunction *thunk = janet_thunk_delay(janet_wrap_nil());
    JanetFiber *fiber = janet_fiber(thunk, 64, 0, NULL);
    fiber->supervisor_channel = janet_root_fiber()->supervisor_channel;
    ow->fiber = fiber;
    janet_async_start_fiber(fiber, stream, JANET_ASYNC_LISTEN_READ, watcher_callback_read, ow);
}

static void janet_watcher_add(JanetWatcher *watcher, const char *path, uint32_t flags) {
    HANDLE handle = CreateFileA(path,
                                FILE_LIST_DIRECTORY | GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED | FILE_FLAG_BACKUP_SEMANTICS,
                                NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        janet_panicv(janet_ev_lasterr());
    }
    JanetStream *stream = janet_stream(handle, JANET_STREAM_READABLE, NULL);
    OverlappedWatch *ow = janet_malloc(sizeof(OverlappedWatch));
    memset(ow, 0, sizeof(OverlappedWatch));
    ow->stream = stream;
    ow->dir_path = janet_cstring(path);
    ow->fiber = NULL;
    Janet pathv = janet_wrap_string(ow->dir_path);
    ow->flags = flags | watcher->default_flags;
    ow->watcher = watcher;
    ow->overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL); /* Do we need this */
    Janet streamv = janet_wrap_pointer(ow);
    janet_table_put(watcher->watch_descriptors, pathv, streamv);
    if (watcher->is_watching) {
        start_listening_ow(ow);
    }
}

static void janet_watcher_remove(JanetWatcher *watcher, const char *path) {
    Janet pathv = janet_cstringv(path);
    Janet streamv = janet_table_get(watcher->watch_descriptors, pathv);
    if (janet_checktype(streamv, JANET_NIL)) {
        janet_panicf("path %v is not being watched", pathv);
    }
    janet_table_remove(watcher->watch_descriptors, pathv);
    OverlappedWatch *ow = janet_unwrap_pointer(streamv);
    janet_stream_close(ow->stream);
}

static void janet_watcher_listen(JanetWatcher *watcher) {
    if (watcher->is_watching) janet_panic("already watching");
    watcher->is_watching = 1;
    for (int32_t i = 0; i < watcher->watch_descriptors->capacity; i++) {
        const JanetKV *kv = watcher->watch_descriptors->data + i;
        if (!janet_checktype(kv->value, JANET_POINTER)) continue;
        OverlappedWatch *ow = janet_unwrap_pointer(kv->value);
        start_listening_ow(ow);
    }
    janet_gcroot(janet_wrap_abstract(watcher));
}

static void janet_watcher_unlisten(JanetWatcher *watcher) {
    if (!watcher->is_watching) return;
    watcher->is_watching = 0;
    for (int32_t i = 0; i < watcher->watch_descriptors->capacity; i++) {
        const JanetKV *kv = watcher->watch_descriptors->data + i;
        if (!janet_checktype(kv->value, JANET_POINTER)) continue;
        OverlappedWatch *ow = janet_unwrap_pointer(kv->value);
        janet_stream_close(ow->stream);
    }
    janet_table_clear(watcher->watch_descriptors);
    janet_gcunroot(janet_wrap_abstract(watcher));
}

#else

/* Default implementation */

static uint32_t decode_watch_flags(const Janet *options, int32_t n) {
    (void) options;
    (void) n;
    return 0;
}

static void janet_watcher_init(JanetWatcher *watcher, JanetChannel *channel, uint32_t default_flags) {
    (void) watcher;
    (void) channel;
    (void) default_flags;
    janet_panic("filewatch not supported on this platform");
}

static void janet_watcher_add(JanetWatcher *watcher, const char *path, uint32_t flags) {
    (void) watcher;
    (void) flags;
    (void) path;
    janet_panic("nyi");
}

static void janet_watcher_remove(JanetWatcher *watcher, const char *path) {
    (void) watcher;
    (void) path;
    janet_panic("nyi");
}

static void janet_watcher_listen(JanetWatcher *watcher) {
    (void) watcher;
    janet_panic("nyi");
}

static void janet_watcher_unlisten(JanetWatcher *watcher) {
    (void) watcher;
    janet_panic("nyi");
}

#endif

/* C Functions */

static int janet_filewatch_mark(void *p, size_t s) {
    JanetWatcher *watcher = (JanetWatcher *) p;
    (void) s;
    if (watcher->channel == NULL) return 0; /* Incomplete initialization */
#ifdef JANET_WINDOWS
    for (int32_t i = 0; i < watcher->watch_descriptors->capacity; i++) {
        const JanetKV *kv = watcher->watch_descriptors->data + i;
        if (!janet_checktype(kv->value, JANET_POINTER)) continue;
        OverlappedWatch *ow = janet_unwrap_pointer(kv->value);
        janet_mark(janet_wrap_fiber(ow->fiber));
        janet_mark(janet_wrap_abstract(ow->stream));
        janet_mark(janet_wrap_string(ow->dir_path));
    }
#else
    janet_mark(janet_wrap_abstract(watcher->stream));
#endif
    janet_mark(janet_wrap_abstract(watcher->channel));
    janet_mark(janet_wrap_table(watcher->watch_descriptors));
    return 0;
}

static const JanetAbstractType janet_filewatch_at = {
    "filewatch/watcher",
    NULL,
    janet_filewatch_mark,
    JANET_ATEND_GCMARK
};

JANET_CORE_FN(cfun_filewatch_make,
              "(filewatch/new channel &opt default-flags)",
              "Create a new filewatcher that will give events to a channel channel. See `filewatch/add` for available flags.\n\n"
              "When an event is triggered by the filewatcher, a struct containing information will be given to channel as with `ev/give`. "
              "The contents of the channel depend on the OS, but will contain some common keys:\n\n"
              "* `:type` -- the type of the event that was raised.\n\n"
              "* `:file-name` -- the base file name of the file that triggered the event.\n\n"
              "* `:dir-name` -- the directory name of the file that triggered the event.\n\n"
              "Events also will contain keys specific to the host OS.\n\n"
              "Windows has no extra properties on events.\n\n"
              "Linux has the following extra properties on events:\n\n"
              "* `:wd` -- the integer key returned by `filewatch/add` for the path that triggered this.\n\n"
              "* `:wd-path` -- the string path for watched directory of file. For files, will be the same as `:file-name`, and for directories, will be the same as `:dir-name`.\n\n"
              "* `:cookie` -- a randomized integer used to associate related events, such as :moved-from and :moved-to events.\n\n"
              "") {
    janet_sandbox_assert(JANET_SANDBOX_FS_READ);
    janet_arity(argc, 1, -1);
    JanetChannel *channel = janet_getchannel(argv, 0);
    JanetWatcher *watcher = janet_abstract(&janet_filewatch_at, sizeof(JanetWatcher));
    uint32_t default_flags = decode_watch_flags(argv + 1, argc - 1);
    janet_watcher_init(watcher, channel, default_flags);
    return janet_wrap_abstract(watcher);
}

JANET_CORE_FN(cfun_filewatch_add,
              "(filewatch/add watcher path &opt flags)",
              "Add a path to the watcher. Available flags depend on the current OS, and are as follows:\n\n"
              "Windows/MINGW (flags correspond to FILE_NOTIFY_CHANGE_* flags in win32 documentation):\n\n"
              "* `:all` - trigger an event for all of the below triggers.\n\n"
              "* `:attributes` - FILE_NOTIFY_CHANGE_ATTRIBUTES\n\n"
              "* `:creation` - FILE_NOTIFY_CHANGE_CREATION\n\n"
              "* `:dir-name` - FILE_NOTIFY_CHANGE_DIR_NAME\n\n"
              "* `:last-access` - FILE_NOTIFY_CHANGE_LAST_ACCESS\n\n"
              "* `:last-write` - FILE_NOTIFY_CHANGE_LAST_WRITE\n\n"
              "* `:security` - FILE_NOTIFY_CHANGE_SECURITY\n\n"
              "* `:size` - FILE_NOTIFY_CHANGE_SIZE\n\n"
              "* `:recursive` - watch subdirectories recursively\n\n"
              "Linux (flags correspond to IN_* flags from <sys/inotify.h>):\n\n"
              "* `:access` - IN_ACCESS\n\n"
              "* `:all` - IN_ALL_EVENTS\n\n"
              "* `:attrib` - IN_ATTRIB\n\n"
              "* `:close-nowrite` - IN_CLOSE_NOWRITE\n\n"
              "* `:close-write` - IN_CLOSE_WRITE\n\n"
              "* `:create` - IN_CREATE\n\n"
              "* `:delete` - IN_DELETE\n\n"
              "* `:delete-self` - IN_DELETE_SELF\n\n"
              "* `:ignored` - IN_IGNORED\n\n"
              "* `:modify` - IN_MODIFY\n\n"
              "* `:move-self` - IN_MOVE_SELF\n\n"
              "* `:moved-from` - IN_MOVED_FROM\n\n"
              "* `:moved-to` - IN_MOVED_TO\n\n"
              "* `:open` - IN_OPEN\n\n"
              "* `:q-overflow` - IN_Q_OVERFLOW\n\n"
              "* `:unmount` - IN_UNMOUNT\n\n\n"
              "On Windows, events will have the following possible types:\n\n"
              "* `:unknown`\n\n"
              "* `:added`\n\n"
              "* `:removed`\n\n"
              "* `:modified`\n\n"
              "* `:renamed-old`\n\n"
              "* `:renamed-new`\n\n"
              "On Linux, events will a `:type` corresponding to the possible flags, excluding `:all`.\n"
              "") {
    janet_arity(argc, 2, -1);
    JanetWatcher *watcher = janet_getabstract(argv, 0, &janet_filewatch_at);
    const char *path = janet_getcstring(argv, 1);
    uint32_t flags = watcher->default_flags | decode_watch_flags(argv + 2, argc - 2);
    janet_watcher_add(watcher, path, flags);
    return argv[0];
}

JANET_CORE_FN(cfun_filewatch_remove,
              "(filewatch/remove watcher path)",
              "Remove a path from the watcher.") {
    janet_fixarity(argc, 2);
    JanetWatcher *watcher = janet_getabstract(argv, 0, &janet_filewatch_at);
    const char *path = janet_getcstring(argv, 1);
    janet_watcher_remove(watcher, path);
    return argv[0];
}

JANET_CORE_FN(cfun_filewatch_listen,
              "(filewatch/listen watcher)",
              "Listen for changes in the watcher.") {
    janet_fixarity(argc, 1);
    JanetWatcher *watcher = janet_getabstract(argv, 0, &janet_filewatch_at);
    janet_watcher_listen(watcher);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_filewatch_unlisten,
              "(filewatch/unlisten watcher)",
              "Stop listening for changes on a given watcher.") {
    janet_fixarity(argc, 1);
    JanetWatcher *watcher = janet_getabstract(argv, 0, &janet_filewatch_at);
    janet_watcher_unlisten(watcher);
    return janet_wrap_nil();
}

/* Module entry point */
void janet_lib_filewatch(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("filewatch/new", cfun_filewatch_make),
        JANET_CORE_REG("filewatch/add", cfun_filewatch_add),
        JANET_CORE_REG("filewatch/remove", cfun_filewatch_remove),
        JANET_CORE_REG("filewatch/listen", cfun_filewatch_listen),
        JANET_CORE_REG("filewatch/unlisten", cfun_filewatch_unlisten),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
}

#endif
#endif
