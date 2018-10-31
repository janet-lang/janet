/*
* Copyright (c) 2018 Calvin Rose
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

#include "line.h"

/* Common */
int janet_line_getter(JanetArgs args) {
    JANET_FIXARITY(args, 2);
    JANET_CHECK(args, 0, JANET_STRING);
    JANET_CHECK(args, 1, JANET_BUFFER);
    janet_line_get(
            janet_unwrap_string(args.v[0]),
            janet_unwrap_buffer(args.v[1]));
    JANET_RETURN(args, args.v[0]);
}

static void simpleline(JanetBuffer *buffer) {
    buffer->count = 0;
    char c;
    for (;;) {
        c = fgetc(stdin);
        if (feof(stdin) || c < 0) {
            break;
        }
        janet_buffer_push_u8(buffer, (uint8_t) c);
        if (c == '\n') break;
    }
}

/* Windows */
#ifdef JANET_WINDOWS

void janet_line_init() {
    ;
}

void janet_line_deinit() {
    ;
}

void janet_line_get(const uint8_t *p, JanetBuffer *buffer) {
    fputs((const char *)p, stdout);
    simpleline(buffer);
}

/* Posix */
#else

/*
https://github.com/antirez/linenoise/blob/master/linenoise.c
*/

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

/* static state */
#define JANET_LINE_MAX 1024
#define JANET_HISTORY_MAX 100
static int israwmode = 0;
static const char *prompt = "> ";
static int plen = 2;
static char buf[JANET_LINE_MAX];
static int len = 0;
static int pos = 0;
static int cols = 80;
static char *history[JANET_HISTORY_MAX];
static int history_count = 0;
static int historyi = 0;
static struct termios termios_start;

/* Unsupported terminal list from linenoise */
static const char *badterms[] = {
    "cons25",
    "dumb",
    "emacs",
    NULL
};

static char *sdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *mem = malloc(len);
    if (!mem) {
        return NULL;
    }
    return memcpy(mem, s, len);
}

/* Ansi terminal raw mode */
static int rawmode() {
    struct termios t;
    if (!isatty(STDIN_FILENO)) goto fatal;
    if (tcgetattr(STDIN_FILENO, &termios_start) == -1) goto fatal;
    t = termios_start;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~(OPOST);
    t.c_cflag |= (CS8);
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) goto fatal;
    israwmode = 1;
    return 0;
fatal:
    errno = ENOTTY;
    return -1;
}

/* Disable raw mode */
static void norawmode() {
    if (israwmode && tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_start) != -1)
        israwmode = 0;
}

static int curpos() {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf)-1) {
        if (read(STDIN_FILENO, buf+i, 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != 27 || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) return -1;
    return cols;
}

static int getcols() {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        int start, cols;
        start = curpos();
        if (start == -1) goto failed;
        if (write(STDOUT_FILENO, "\x1b[999C", 6) != 6) goto failed;
        cols = curpos();
        if (cols == -1) goto failed;
        if (cols > start) {
            char seq[32];
            snprintf(seq, 32, "\x1b[%dD", cols-start);
            if (write(STDOUT_FILENO, seq, strlen(seq)) == -1) {}
        }
        return cols;
    } else {
        return ws.ws_col;
    }
failed:
    return 80;
}

static void clear() {
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {}
}

static void refresh() {
    char seq[64];
    JanetBuffer b;
 
    /* Keep cursor position on screen */
    char *_buf = buf;
    int _len = len;
    int _pos = pos;
    while ((plen + _pos) >= cols) {
        _buf++;
        _len--;
        _pos--;
    }
    while ((plen + _len) > cols) {
        _len--;
    }

    janet_buffer_init(&b, 0);
    /* Cursor to left edge, prompt and buffer */
    janet_buffer_push_u8(&b, '\r');
    janet_buffer_push_cstring(&b, prompt);
    janet_buffer_push_bytes(&b, (uint8_t *) _buf, _len);
    /* Erase to right */
    janet_buffer_push_cstring(&b, "\x1b[0K");
    /* Move cursor to original position. */
    snprintf(seq, 64,"\r\x1b[%dC", (int)(_pos + plen));
    janet_buffer_push_cstring(&b, seq);
    if (write(STDOUT_FILENO, b.data, b.count) == -1) {}
    janet_buffer_deinit(&b);
}

static int insert(char c) {
    if (len < JANET_LINE_MAX - 1) {
        if (len == pos) {
            buf[pos++] = c;
            buf[++len] = '\0';
            if (plen + len < cols) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                if (write(STDOUT_FILENO, &c, 1) == -1) return -1;
            } else {
                refresh();
            }
        } else {
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos++] = c;
            buf[++len] = '\0';
            refresh();
        }
    }
    return 0;
}

static void historymove(int delta) {
    if (history_count > 1) {
        free(history[historyi]);
        history[historyi] = sdup(buf);

        historyi += delta;
        if (historyi < 0) {
            historyi = 0;
            return;
        } else if (historyi >= history_count) {
            historyi = history_count - 1;
            return;
        }
        strncpy(buf, history[historyi], JANET_LINE_MAX - 1);
        pos = len = strlen(buf);
        buf[len] = '\0';

        refresh();
    }
}

static void addhistory() {
    int i, len;
    char *newline = sdup(buf);
    if (!newline) return;
    len = history_count;
    if (len < JANET_HISTORY_MAX) {
        history[history_count++] = newline;
        len++;
    } else {
        free(history[JANET_HISTORY_MAX - 1]);
    }
    for (i = len - 1; i > 0; i--) {
        history[i] = history[i - 1];
    }
    history[0] = newline;
}

static void replacehistory() {
    char *newline = sdup(buf);
    if (!newline) return;
    free(history[0]);
    history[0] = newline;
}

static void kleft() {
    if (pos > 0) {
        pos--;
        refresh();
    }
}

static void kright() {
    if (pos != len) {
        pos++;
        refresh();
    }
}

static void kbackspace() {
    if (pos > 0) {
        memmove(buf + pos - 1, buf + pos, len - pos);
        pos--; 
        buf[--len] = '\0';
        refresh();
    }
}

static int line() {
    cols = getcols();
    plen = 0;
    len = 0;
    pos = 0;
    while (prompt[plen]) plen++;
    buf[0] = '\0';

    addhistory();

    if (write(STDOUT_FILENO, prompt, plen) == -1) return -1;
    for (;;) {
        char c;
        int nread;
        char seq[3];

        nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) return -1;

        switch(c) {
        default:
            if (insert(c)) return -1;
            break;
        case 9:     /* tab */
            if (insert(' ')) return -1;
            if (insert(' ')) return -1;
            break;
        case 13:    /* enter */
            return 0;
        case 3:     /* ctrl-c */
            errno = EAGAIN;
            return -1;
        case 127:   /* backspace */
        case 8:     /* ctrl-h */
            kbackspace();
            break;
        case 4:     /* ctrl-d, eof */
            return -1;
        case 2:     /* ctrl-b */
            kleft();
            break;
        case 6:     /* ctrl-f */
            kright();
            break;
        case 21:
            buf[0] = '\0';
            pos = len = 0;
            refresh();
            break;
        case 26: /* ctrl-z */
            norawmode();
            kill(getpid(), SIGSTOP);
            rawmode();
            refresh();
            break;
        case 12:
            clear();
            refresh();
            break;
        case 27:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence.
             * Use two calls to handle slow terminals returning the two
             * chars at different times. */
            if (read(STDIN_FILENO, seq, 1) == -1) break;
            if (read(STDIN_FILENO, seq + 1, 1) == -1) break;
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional byte. */
                    if (read(STDIN_FILENO, seq + 2, 1) == -1) break;
                    if (seq[2] == '~') {
                        switch(seq[1]) {
                        default:
                            break;
                        }
                    }
                } else {
                    switch (seq[1]) {
                    default:
                        break;
                    case 'A':
                        historymove(1);
                        break;
                    case 'B':
                        historymove(-1);
                        break;
                    case 'C': /* Right */
                        kright();
                        break;
                    case 'D': /* Left */
                        kleft();
                        break;
                    case 'H':
                        pos = 0;
                        refresh();
                        break;
                    case 'F':
                        pos = len;
                        refresh();
                        break;
                    }
                }
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                default:
                    break;
                case 'H':
                    pos = 0;
                    refresh();
                    break;
                case 'F':
                    pos = len;
                    refresh();
                    break;
                }
            }
            break;
        }
    }
    return 0;
}

void janet_line_init() {
    ;
}

void janet_line_deinit() {
    int i;
    norawmode();
    for (i = 0; i < history_count; i++)
        free(history[i]);
    historyi = 0;
}

static int checktermsupport() {
    const char *t = getenv("TERM");
    int i;
    if (!t) return 1;
    for (i = 0; badterms[i]; i++)
        if (!strcmp(t, badterms[i])) return 0;
    return 1;
}

void janet_line_get(const uint8_t *p, JanetBuffer *buffer) {
    prompt = (const char *)p; 
    buffer->count = 0;
    historyi = 0;
    if (!isatty(STDIN_FILENO) || !checktermsupport()) {
        simpleline(buffer);
        return;
    }
    if (rawmode()) {
        simpleline(buffer);
        return;
    }
    if (line()) {
        norawmode();
        fputc('\n', stdout);
        return;
    }
    norawmode();
    fputc('\n', stdout);
    janet_buffer_ensure(buffer, len + 1);
    memcpy(buffer->data, buf, len);
    buffer->data[len] = '\n';
    buffer->count = len + 1;
    replacehistory();
}

#endif
