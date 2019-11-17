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
#include "line.h"
#endif

/* Common */
Janet janet_line_getter(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 2);
    const char *str = (argc >= 1) ? (const char *) janet_getstring(argv, 0) : "";
    JanetBuffer *buf = (argc >= 2) ? janet_getbuffer(argv, 1) : janet_buffer(10);
    janet_line_get(str, buf);
    return janet_wrap_buffer(buf);
}

static void simpleline(JanetBuffer *buffer) {
    FILE *in = janet_dynfile("in", stdin);
    buffer->count = 0;
    int c;
    for (;;) {
        c = fgetc(in);
        if (feof(in) || c < 0) {
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

void janet_line_get(const char *p, JanetBuffer *buffer) {
    FILE *out = janet_dynfile("out", stdout);
    fputs(p, out);
    fflush(out);
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
static int gbl_israwmode = 0;
static const char *gbl_prompt = "> ";
static int gbl_plen = 2;
static char gbl_buf[JANET_LINE_MAX];
static int gbl_len = 0;
static int gbl_pos = 0;
static int gbl_cols = 80;
static char *gbl_history[JANET_HISTORY_MAX];
static int gbl_history_count = 0;
static int gbl_historyi = 0;
static int gbl_sigint_flag = 0;
static struct termios gbl_termios_start;

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
    if (tcgetattr(STDIN_FILENO, &gbl_termios_start) == -1) goto fatal;
    t = gbl_termios_start;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~(OPOST);
    t.c_cflag |= (CS8);
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) goto fatal;
    gbl_israwmode = 1;
    return 0;
fatal:
    errno = ENOTTY;
    return -1;
}

/* Disable raw mode */
static void norawmode() {
    if (gbl_israwmode && tcsetattr(STDIN_FILENO, TCSAFLUSH, &gbl_termios_start) != -1)
        gbl_israwmode = 0;
}

static int curpos() {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, buf + i, 1) != 1) break;
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
            snprintf(seq, 32, "\x1b[%dD", cols - start);
            if (write(STDOUT_FILENO, seq, strlen(seq)) == -1) {
                exit(1);
            }
        }
        return cols;
    } else {
        return ws.ws_col;
    }
failed:
    return 80;
}

static void clear() {
    if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0) {
        exit(1);
    }
}

static void refresh() {
    char seq[64];
    JanetBuffer b;

    /* Keep cursor position on screen */
    char *_buf = gbl_buf;
    int _len = gbl_len;
    int _pos = gbl_pos;
    while ((gbl_plen + _pos) >= gbl_cols) {
        _buf++;
        _len--;
        _pos--;
    }
    while ((gbl_plen + _len) > gbl_cols) {
        _len--;
    }

    janet_buffer_init(&b, 0);
    /* Cursor to left edge, gbl_prompt and buffer */
    janet_buffer_push_u8(&b, '\r');
    janet_buffer_push_cstring(&b, gbl_prompt);
    janet_buffer_push_bytes(&b, (uint8_t *) _buf, _len);
    /* Erase to right */
    janet_buffer_push_cstring(&b, "\x1b[0K");
    /* Move cursor to original position. */
    snprintf(seq, 64, "\r\x1b[%dC", (int)(_pos + gbl_plen));
    janet_buffer_push_cstring(&b, seq);
    if (write(STDOUT_FILENO, b.data, b.count) == -1) {
        exit(1);
    }
    janet_buffer_deinit(&b);
}

static int insert(char c) {
    if (gbl_len < JANET_LINE_MAX - 1) {
        if (gbl_len == gbl_pos) {
            gbl_buf[gbl_pos++] = c;
            gbl_buf[++gbl_len] = '\0';
            if (gbl_plen + gbl_len < gbl_cols) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                if (write(STDOUT_FILENO, &c, 1) == -1) return -1;
            } else {
                refresh();
            }
        } else {
            memmove(gbl_buf + gbl_pos + 1, gbl_buf + gbl_pos, gbl_len - gbl_pos);
            gbl_buf[gbl_pos++] = c;
            gbl_buf[++gbl_len] = '\0';
            refresh();
        }
    }
    return 0;
}

static void historymove(int delta) {
    if (gbl_history_count > 1) {
        free(gbl_history[gbl_historyi]);
        gbl_history[gbl_historyi] = sdup(gbl_buf);

        gbl_historyi += delta;
        if (gbl_historyi < 0) {
            gbl_historyi = 0;
            return;
        } else if (gbl_historyi >= gbl_history_count) {
            gbl_historyi = gbl_history_count - 1;
            return;
        }
        strncpy(gbl_buf, gbl_history[gbl_historyi], JANET_LINE_MAX - 1);
        gbl_pos = gbl_len = strlen(gbl_buf);
        gbl_buf[gbl_len] = '\0';

        refresh();
    }
}

static void addhistory() {
    int i, len;
    char *newline = sdup(gbl_buf);
    if (!newline) return;
    len = gbl_history_count;
    if (len < JANET_HISTORY_MAX) {
        gbl_history[gbl_history_count++] = newline;
        len++;
    } else {
        free(gbl_history[JANET_HISTORY_MAX - 1]);
    }
    for (i = len - 1; i > 0; i--) {
        gbl_history[i] = gbl_history[i - 1];
    }
    gbl_history[0] = newline;
}

static void replacehistory() {
    char *newline = sdup(gbl_buf);
    if (!newline) return;
    free(gbl_history[0]);
    gbl_history[0] = newline;
}

static void kleft() {
    if (gbl_pos > 0) {
        gbl_pos--;
        refresh();
    }
}

static void kright() {
    if (gbl_pos != gbl_len) {
        gbl_pos++;
        refresh();
    }
}

static void kbackspace() {
    if (gbl_pos > 0) {
        memmove(gbl_buf + gbl_pos - 1, gbl_buf + gbl_pos, gbl_len - gbl_pos);
        gbl_pos--;
        gbl_buf[--gbl_len] = '\0';
        refresh();
    }
}

static void kdelete() {
    if (gbl_pos != gbl_len) {
        memmove(gbl_buf + gbl_pos, gbl_buf + gbl_pos + 1, gbl_len - gbl_pos);
        gbl_buf[--gbl_len] = '\0';
        refresh();
    }
}

static int line() {
    gbl_cols = getcols();
    gbl_plen = 0;
    gbl_len = 0;
    gbl_pos = 0;
    while (gbl_prompt[gbl_plen]) gbl_plen++;
    gbl_buf[0] = '\0';

    addhistory();

    if (write(STDOUT_FILENO, gbl_prompt, gbl_plen) == -1) return -1;
    for (;;) {
        char c;
        int nread;
        char seq[3];

        nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) return -1;

        switch (c) {
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
                gbl_sigint_flag = 1;
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
                gbl_buf[0] = '\0';
                gbl_pos = gbl_len = 0;
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
                            switch (seq[1]) {
                                case '3': /* delete */
                                    kdelete();
                                    break;
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
                                gbl_pos = 0;
                                refresh();
                                break;
                            case 'F':
                                gbl_pos = gbl_len;
                                refresh();
                                break;
                        }
                    }
                } else if (seq[0] == 'O') {
                    switch (seq[1]) {
                        default:
                            break;
                        case 'H':
                            gbl_pos = 0;
                            refresh();
                            break;
                        case 'F':
                            gbl_pos = gbl_len;
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
    for (i = 0; i < gbl_history_count; i++)
        free(gbl_history[i]);
    gbl_historyi = 0;
}

static int checktermsupport() {
    const char *t = getenv("TERM");
    int i;
    if (!t) return 1;
    for (i = 0; badterms[i]; i++)
        if (!strcmp(t, badterms[i])) return 0;
    return 1;
}

void janet_line_get(const char *p, JanetBuffer *buffer) {
    gbl_prompt = p;
    buffer->count = 0;
    gbl_historyi = 0;
    FILE *out = janet_dynfile("out", stdout);
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
        if (gbl_sigint_flag) {
            raise(SIGINT);
        } else {
            fputc('\n', out);
        }
        return;
    }
    norawmode();
    fputc('\n', out);
    janet_buffer_ensure(buffer, gbl_len + 1, 2);
    memcpy(buffer->data, gbl_buf, gbl_len);
    buffer->data[gbl_len] = '\n';
    buffer->count = gbl_len + 1;
    replacehistory();
}

#endif
