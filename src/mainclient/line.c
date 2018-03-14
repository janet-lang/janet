/*
* Copyright (c) 2017 Calvin Rose
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
int dst_line_getter(DstArgs args) {
    if (args.n < 1 || !dst_checktype(args.v[0], DST_BUFFER))
        return dst_throw(args, "expected buffer");
    dst_line_get(dst_unwrap_buffer(args.v[0]));
    return dst_return(args, args.v[0]);
}

static void simpleline(DstBuffer *buffer) {
    buffer->count = 0;
    char c;
    for (;;) {
        c = fgetc(stdin);
        dst_buffer_push_u8(buffer, (uint8_t) c);
        if (c == '\n') break;
    }
}

/* Windows */
#ifdef DST_WINDOWS

void dst_line_init() {
    ;
}

void dst_line_deinit() {
    ;
}

void dst_line_get(DstBuffer *buffer) {
    fputs(">> ", stdout);
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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <headerlibs/vector.h>

/* static state */
#define DST_LINE_MAX 1024
#define DST_HISTORY_MAX 100
static int israwmode = 0;
static const char *prompt = ">> ";
static int plen = 3;
static char buf[DST_LINE_MAX];
static int len = 0;
static int pos = 0;
static int cols = 80;
static char **history = NULL;
static int historyi = 0;
static struct termios termios_start;

/* Key codes */
enum KEY_ACTION {
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

/* Ansi terminal raw mode */
static int rawmode() {
    struct termios raw;
    if (!isatty(STDIN_FILENO)) goto fatal;
    if (tcgetattr(STDIN_FILENO, &termios_start) == -1) goto fatal;
    raw = termios_start;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) goto fatal;
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
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) return -1;
    return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
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

/* Clear the screen. Used to handle ctrl+l */
static void clear() {
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {}
}

/* Refresh the line */
static void refresh() {
    char seq[64];
    DstBuffer b;
 
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

    dst_buffer_init(&b, 0);
    /* Cursor to left edge, prompt and buffer */
    dst_buffer_push_u8(&b, '\r');
    dst_buffer_push_cstring(&b, prompt);
    dst_buffer_push_bytes(&b, (uint8_t *) _buf, _len);
    /* Erase to right */
    dst_buffer_push_cstring(&b, "\x1b[0K");
    /* Move cursor to original position. */
    snprintf(seq, 64,"\r\x1b[%dC", (int)(_pos + plen));
    dst_buffer_push_cstring(&b, seq);
    if (write(STDOUT_FILENO, b.data, b.count) == -1) {}
    dst_buffer_deinit(&b);
}

static int insert(char c) {
    if (len < DST_LINE_MAX - 1) {
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
    if (dst_v_count(history) > 1) {
        free(history[historyi]);
        history[historyi] = strdup(buf);

        historyi += delta;
        if (historyi < 0) {
            historyi = 0;
            return;
        } else if (historyi >= dst_v_count(history)) {
            historyi = dst_v_count(history) - 1;
            return;
        }
        strncpy(buf, history[historyi], DST_LINE_MAX);
        pos = len = strlen(buf);
        buf[len] = '\0';

        refresh();
    }
}

static void addhistory() {
    int i, len;
    char *newline = strdup(buf);
    if (!newline) return;
    len = dst_v_count(history);
    if (len < DST_HISTORY_MAX) {
        dst_v_push(history, newline);
        len++;
    }
    for (i = len - 1; i > 0; i--) {
        history[i] = history[i - 1];
    }
    history[0] = newline;
}

static void replacehistory() {
    char *newline = strdup(buf);
    if (!newline) return;
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
        case ENTER:    /* enter */
            return 0;
        case CTRL_C:     /* ctrl-c */
            errno = EAGAIN;
            return -1;
        case BACKSPACE:   /* backspace */
        case 8:     /* ctrl-h */
            kbackspace();
            break;
        case CTRL_D:     /* ctrl-d, eof */
            return -1;
        case CTRL_B:     /* ctrl-b */
            kleft();
            break;
        case CTRL_F:     /* ctrl-f */
            kright();
            break;
        case ESC:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence.
             * Use two calls to handle slow terminals returning the two
             * chars at different times. */
            if (read(STDIN_FILENO, seq, 1) == -1) break;
            if (read(STDIN_FILENO, seq + 1 ,1) == -1) break;

            /* ESC [ sequences. */
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
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O') {
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
        default:
            if (insert(c)) return -1;
            break;
        case CTRL_U:
            buf[0] = '\0';
            pos = len = 0;
            refresh();
            break;
        case CTRL_L:
            clear();
            refresh();
            break;
        }
    }
    return 0;
}

void dst_line_init() {
    ;
}

void dst_line_deinit() {
    norawmode();
    dst_v_free(history);
}

void dst_line_get(DstBuffer *buffer) {
    buffer->count = 0;
    historyi = 0;
    if (rawmode()) {
        simpleline(buffer);
        return;
    }
    if (line()) {
        norawmode();
        exit(0);
        return;
    }
    norawmode();
    fputc('\n', stdout);
    dst_buffer_ensure(buffer, len + 1);
    memcpy(buffer->data, buf, len);
    buffer->data[len] = '\n';
    buffer->count = len + 1;

    replacehistory();
}

#endif
