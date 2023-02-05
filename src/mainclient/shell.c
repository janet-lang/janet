/*
* Copyright (c) 2023 Calvin Rose
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

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

#include <janet.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#endif

void janet_line_init();
void janet_line_deinit();

void janet_line_get(const char *p, JanetBuffer *buffer);
Janet janet_line_getter(int32_t argc, Janet *argv);

static JANET_THREAD_LOCAL int gbl_cancel_current_repl_form = 0;

/*
 * Line Editing
 */

static JANET_THREAD_LOCAL JanetTable *gbl_complete_env;

/* Common */
Janet janet_line_getter(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 3);
    const char *str = (argc >= 1) ? (const char *) janet_getstring(argv, 0) : "";
    JanetBuffer *buf = (argc >= 2) ? janet_getbuffer(argv, 1) : janet_buffer(10);
    gbl_complete_env = (argc >= 3) ? janet_gettable(argv, 2) : NULL;
    janet_line_get(str, buf);
    gbl_complete_env = NULL;

    Janet result;
    if (gbl_cancel_current_repl_form) {
        gbl_cancel_current_repl_form = 0;

        /* Signal that the user bailed out of the current form */
        result = janet_ckeywordv("cancel");
    } else {
        result = janet_wrap_buffer(buf);
    }
    return result;
}

static void simpleline(JanetBuffer *buffer) {
    FILE *in = janet_dynfile("in", stdin);
    buffer->count = 0;
    int c;
    for (;;) {
        c = fgetc(in);
        if (c < 0 && !feof(in) && errno == EINTR) {
            continue;
        }
        if (feof(in) || c < 0) {
            break;
        }
        janet_buffer_push_u8(buffer, (uint8_t) c);
        if (c == '\n') break;
    }
}

/* State */

#ifndef JANET_SIMPLE_GETLINE
/* static state */
#define JANET_LINE_MAX 1024
#define JANET_MATCH_MAX 256
#define JANET_HISTORY_MAX 100
static JANET_THREAD_LOCAL int gbl_israwmode = 0;
static JANET_THREAD_LOCAL const char *gbl_prompt = "> ";
static JANET_THREAD_LOCAL int gbl_plen = 2;
static JANET_THREAD_LOCAL char gbl_buf[JANET_LINE_MAX];
static JANET_THREAD_LOCAL int gbl_len = 0;
static JANET_THREAD_LOCAL int gbl_pos = 0;
static JANET_THREAD_LOCAL int gbl_cols = 80;
static JANET_THREAD_LOCAL char *gbl_history[JANET_HISTORY_MAX];
static JANET_THREAD_LOCAL int gbl_history_count = 0;
static JANET_THREAD_LOCAL int gbl_historyi = 0;
static JANET_THREAD_LOCAL JanetByteView gbl_matches[JANET_MATCH_MAX];
static JANET_THREAD_LOCAL int gbl_match_count = 0;
static JANET_THREAD_LOCAL int gbl_lines_below = 0;
#endif

/* Fallback */
#if defined(JANET_SIMPLE_GETLINE)

void janet_line_init() {
    ;
}

void janet_line_deinit() {
    ;
}

void janet_line_get(const char *p, JanetBuffer *buffer) {
    FILE *out = janet_dynfile("err", stderr);
    fputs(p, out);
    fflush(out);
    simpleline(buffer);
}

/* Rich implementation */
#else

/* Windows */
#ifdef _WIN32

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <io.h>

static void setup_console_output(void) {
    /* Enable color console on windows 10 console and utf8 output and other processing */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    SetConsoleOutputCP(65001);
}

/* Ansi terminal raw mode */
static int rawmode(void) {
    if (gbl_israwmode) return 0;
    HANDLE hOut = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode &= ~ENABLE_LINE_INPUT;
    dwMode &= ~ENABLE_INSERT_MODE;
    dwMode &= ~ENABLE_ECHO_INPUT;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    dwMode &= ~ENABLE_PROCESSED_INPUT;
    if (!SetConsoleMode(hOut, dwMode)) return 1;
    gbl_israwmode = 1;
    return 0;
}

/* Disable raw mode */
static void norawmode(void) {
    if (!gbl_israwmode) return;
    HANDLE hOut = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_LINE_INPUT;
    dwMode |= ENABLE_INSERT_MODE;
    dwMode |= ENABLE_ECHO_INPUT;
    dwMode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    dwMode |= ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hOut, dwMode);
    gbl_israwmode = 0;
}

static long write_console(const char *bytes, size_t n) {
    DWORD nwritten = 0;
    BOOL result = WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), bytes, (DWORD) n, &nwritten, NULL);
    if (!result) return -1; /* error */
    return (long)nwritten;
}

static long read_console(char *into, size_t n) {
    DWORD numread;
    BOOL result = ReadConsole(GetStdHandle(STD_INPUT_HANDLE), into, (DWORD) n, &numread, NULL);
    if (!result) return -1; /* error */
    return (long)numread;
}

static int check_simpleline(JanetBuffer *buffer) {
    if (!_isatty(_fileno(stdin)) || rawmode()) {
        simpleline(buffer);
        return 1;
    }
    return 0;
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
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

static JANET_THREAD_LOCAL struct termios gbl_termios_start;

/* Unsupported terminal list from linenoise */
static const char *badterms[] = {
    "cons25",
    "dumb",
    "emacs",
    NULL
};

/* Ansi terminal raw mode */
static int rawmode(void) {
    struct termios t;
    if (!isatty(STDIN_FILENO)) goto fatal;
    if (tcgetattr(STDIN_FILENO, &gbl_termios_start) == -1) goto fatal;
    t = gbl_termios_start;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_cflag |= (CS8);
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &t) < 0) goto fatal;
    gbl_israwmode = 1;
    return 0;
fatal:
    errno = ENOTTY;
    return -1;
}

/* Disable raw mode */
static void norawmode(void) {
    if (gbl_israwmode && tcsetattr(STDIN_FILENO, TCSADRAIN, &gbl_termios_start) != -1)
        gbl_israwmode = 0;
}

static int checktermsupport() {
    const char *t = getenv("TERM");
    int i;
    if (!t) return 1;
    for (i = 0; badterms[i]; i++)
        if (!strcmp(t, badterms[i])) return 0;
    return 1;
}

static long write_console(char *bytes, size_t n) {
    return write(STDOUT_FILENO, bytes, n);
}

static long read_console(char *into, size_t n) {
    return read(STDIN_FILENO, into, n);
}

static int check_simpleline(JanetBuffer *buffer) {
    if (!isatty(STDIN_FILENO) || !checktermsupport()) {
        simpleline(buffer);
        return 1;
    }
    if (rawmode()) {
        simpleline(buffer);
        return 1;
    }
    return 0;
}

#endif

static char *sdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *mem = janet_malloc(len);
    if (!mem) {
        return NULL;
    }
    return memcpy(mem, s, len);
}

#ifndef _WIN32
static int curpos(void) {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;
    if (write_console("\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf) - 1) {
        if (read_console(buf + i, 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != 27 || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) return -1;
    return cols;
}
#endif

static int getcols(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return (int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
#else
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        int start, cols;
        start = curpos();
        if (start == -1) goto failed;
        if (write_console("\x1b[999C", 6) != 6) goto failed;
        cols = curpos();
        if (cols == -1) goto failed;
        if (cols > start) {
            char seq[32];
            snprintf(seq, 32, "\x1b[%dD", cols - start);
            if (write_console(seq, strlen(seq)) == -1) {
                exit(1);
            }
        }
        return cols;
    } else {
        return ws.ws_col;
    }
failed:
    return 80;
#endif
}

static void clear(void) {
    if (write_console("\x1b[H\x1b[2J", 7) <= 0) {
        exit(1);
    }
}

static void refresh(void) {
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
    if (write_console((char *) b.data, b.count) == -1) {
        exit(1);
    }
    janet_buffer_deinit(&b);
}

static void clearlines(void) {
    for (int i = 0; i < gbl_lines_below; i++) {
        fprintf(stderr, "\x1b[1B\x1b[999D\x1b[K");
    }
    if (gbl_lines_below) {
        fprintf(stderr, "\x1b[%dA\x1b[999D", gbl_lines_below);
        fflush(stderr);
        gbl_lines_below = 0;
    }
}

static int insert(char c, int draw) {
    if (gbl_len < JANET_LINE_MAX - 1) {
        if (gbl_len == gbl_pos) {
            gbl_buf[gbl_pos++] = c;
            gbl_buf[++gbl_len] = '\0';
            if (draw) {
                if (gbl_plen + gbl_len < gbl_cols) {
                    /* Avoid a full update of the line in the
                     * trivial case. */
                    if (write_console(&c, 1) == -1) return -1;
                } else {
                    refresh();
                }
            }
        } else {
            memmove(gbl_buf + gbl_pos + 1, gbl_buf + gbl_pos, gbl_len - gbl_pos);
            gbl_buf[gbl_pos++] = c;
            gbl_buf[++gbl_len] = '\0';
            if (draw) refresh();
        }
    }
    return 0;
}

static void historymove(int delta) {
    if (gbl_history_count > 1) {
        janet_free(gbl_history[gbl_historyi]);
        gbl_history[gbl_historyi] = sdup(gbl_buf);

        gbl_historyi += delta;
        if (gbl_historyi < 0) {
            gbl_historyi = 0;
        } else if (gbl_historyi >= gbl_history_count) {
            gbl_historyi = gbl_history_count - 1;
        }
        strncpy(gbl_buf, gbl_history[gbl_historyi], JANET_LINE_MAX - 1);
        gbl_pos = gbl_len = (int) strlen(gbl_buf);
        gbl_buf[gbl_len] = '\0';

        refresh();
    }
}

static void addhistory(void) {
    int i, len;
    char *newline = sdup(gbl_buf);
    if (!newline) return;
    len = gbl_history_count;
    if (len < JANET_HISTORY_MAX) {
        gbl_history[gbl_history_count++] = newline;
        len++;
    } else {
        janet_free(gbl_history[JANET_HISTORY_MAX - 1]);
    }
    for (i = len - 1; i > 0; i--) {
        gbl_history[i] = gbl_history[i - 1];
    }
    gbl_history[0] = newline;
}

static void replacehistory(void) {
    /* History count is always > 0 here */
    if (gbl_len == 0 || (gbl_history_count > 1 && !strcmp(gbl_buf, gbl_history[1]))) {
        /* Delete history */
        janet_free(gbl_history[0]);
        for (int i = 1; i < gbl_history_count; i++) {
            gbl_history[i - 1] = gbl_history[i];
        }
        gbl_history_count--;
    } else {
        char *newline = sdup(gbl_buf);
        if (!newline) return;
        janet_free(gbl_history[0]);
        gbl_history[0] = newline;
    }
}

static void kleft(void) {
    if (gbl_pos > 0) {
        gbl_pos--;
        refresh();
    }
}

static void kleftw(void) {
    while (gbl_pos > 0 && isspace(gbl_buf[gbl_pos - 1])) {
        gbl_pos--;
    }
    while (gbl_pos > 0 && !isspace(gbl_buf[gbl_pos - 1])) {
        gbl_pos--;
    }
    refresh();
}

static void kright(void) {
    if (gbl_pos != gbl_len) {
        gbl_pos++;
        refresh();
    }
}

static void krightw(void) {
    while (gbl_pos != gbl_len && !isspace(gbl_buf[gbl_pos])) {
        gbl_pos++;
    }
    while (gbl_pos != gbl_len && isspace(gbl_buf[gbl_pos])) {
        gbl_pos++;
    }
    refresh();
}

static void kbackspace(int draw) {
    if (gbl_pos > 0) {
        memmove(gbl_buf + gbl_pos - 1, gbl_buf + gbl_pos, gbl_len - gbl_pos);
        gbl_pos--;
        gbl_buf[--gbl_len] = '\0';
        if (draw) refresh();
    }
}

static void kdelete(int draw) {
    if (gbl_pos != gbl_len) {
        memmove(gbl_buf + gbl_pos, gbl_buf + gbl_pos + 1, gbl_len - gbl_pos);
        gbl_buf[--gbl_len] = '\0';
        if (draw) refresh();
    }
}

static void kbackspacew(void) {
    while (gbl_pos && isspace(gbl_buf[gbl_pos - 1])) {
        kbackspace(0);
    }
    while (gbl_pos && !isspace(gbl_buf[gbl_pos - 1])) {
        kbackspace(0);
    }
    refresh();
}

static void kdeletew(void) {
    while (gbl_pos < gbl_len && isspace(gbl_buf[gbl_pos])) {
        kdelete(0);
    }
    while (gbl_pos < gbl_len && !isspace(gbl_buf[gbl_pos])) {
        kdelete(0);
    }
    refresh();
}


/* See tools/symchargen.c */
static int is_symbol_char_gen(uint8_t c) {
    if (c & 0x80) return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return (c == '!' ||
            c == '$' ||
            c == '%' ||
            c == '&' ||
            c == '*' ||
            c == '+' ||
            c == '-' ||
            c == '.' ||
            c == '/' ||
            c == ':' ||
            c == '<' ||
            c == '?' ||
            c == '=' ||
            c == '>' ||
            c == '@' ||
            c == '^' ||
            c == '_');
}

static JanetByteView get_symprefix(void) {
    /* Calculate current partial symbol. Maybe we could actually hook up the Janet
     * parser here...*/
    int i;
    JanetByteView ret;
    ret.len = 0;
    for (i = gbl_pos - 1; i >= 0; i--) {
        uint8_t c = (uint8_t) gbl_buf[i];
        if (!is_symbol_char_gen(c)) break;
        ret.len++;
    }
    /* Will be const for duration of match checking */
    ret.bytes = (const uint8_t *)(gbl_buf + i + 1);
    return ret;
}

static int compare_bytes(JanetByteView a, JanetByteView b) {
    int32_t minlen = a.len < b.len ? a.len : b.len;
    int result = strncmp((const char *) a.bytes, (const char *) b.bytes, minlen);
    if (result) return result;
    return a.len < b.len ? -1 : a.len > b.len ? 1 : 0;
}

static void check_match(JanetByteView src, const uint8_t *testsym, int32_t testlen) {
    JanetByteView test;
    test.bytes = testsym;
    test.len = testlen;
    if (src.len > test.len || strncmp((const char *) src.bytes, (const char *) test.bytes, src.len)) return;
    JanetByteView mm = test;
    for (int i = 0; i < gbl_match_count; i++) {
        if (compare_bytes(mm, gbl_matches[i]) < 0) {
            JanetByteView temp = mm;
            mm = gbl_matches[i];
            gbl_matches[i] = temp;
        }
    }
    if (gbl_match_count == JANET_MATCH_MAX) return;
    gbl_matches[gbl_match_count++] = mm;
}

static void check_cmatch(JanetByteView src, const char *cstr) {
    check_match(src, (const uint8_t *) cstr, (int32_t) strlen(cstr));
}

static JanetByteView longest_common_prefix(void) {
    JanetByteView bv;
    if (gbl_match_count == 0) {
        bv.len = 0;
        bv.bytes = NULL;
    } else {
        bv = gbl_matches[0];
        for (int i = 0; i < gbl_match_count; i++) {
            JanetByteView other = gbl_matches[i];
            int32_t minlen = other.len < bv.len ? other.len : bv.len;
            for (bv.len = 0; bv.len < minlen; bv.len++) {
                if (bv.bytes[bv.len] != other.bytes[bv.len]) {
                    break;
                }
            }
        }
    }
    return bv;
}

static void check_specials(JanetByteView src) {
    check_cmatch(src, "break");
    check_cmatch(src, "def");
    check_cmatch(src, "do");
    check_cmatch(src, "fn");
    check_cmatch(src, "if");
    check_cmatch(src, "quasiquote");
    check_cmatch(src, "quote");
    check_cmatch(src, "set");
    check_cmatch(src, "splice");
    check_cmatch(src, "unquote");
    check_cmatch(src, "var");
    check_cmatch(src, "while");
    check_cmatch(src, "upscope");
}

static void resolve_format(JanetTable *entry) {
    int is_macro = janet_truthy(janet_table_get(entry, janet_ckeywordv("macro")));
    Janet refv = janet_table_get(entry, janet_ckeywordv("ref"));
    int is_ref = janet_checktype(refv, JANET_ARRAY);
    Janet value = janet_wrap_nil();
    if (is_ref) {
        JanetArray *a = janet_unwrap_array(refv);
        if (a->count) value = a->data[0];
    } else {
        value = janet_table_get(entry, janet_ckeywordv("value"));
    }
    if (is_macro) {
        fprintf(stderr, "    macro\n");
        gbl_lines_below++;
    } else if (is_ref) {
        janet_eprintf("    var (%t)\n", value);
        gbl_lines_below++;
    } else {
        janet_eprintf("    %t\n", value);
        gbl_lines_below++;
    }
    Janet sm = janet_table_get(entry, janet_ckeywordv("source-map"));
    Janet path = janet_get(sm, janet_wrap_integer(0));
    Janet line = janet_get(sm, janet_wrap_integer(1));
    Janet col = janet_get(sm, janet_wrap_integer(2));
    if (janet_checktype(path, JANET_STRING) && janet_truthy(line) && janet_truthy(col)) {
        janet_eprintf("    %S on line %v, column %v\n", janet_unwrap_string(path), line, col);
        gbl_lines_below++;
    }
}

static void doc_format(JanetString doc, int32_t width) {
    int32_t maxcol = width - 8;
    uint8_t wordbuf[256] = {0};
    int32_t wordp = 0;
    int32_t current = 0;
    if (maxcol > 200) maxcol = 200;
    fprintf(stderr, "    ");
    for (int32_t i = 0; i < janet_string_length(doc); i++) {
        uint8_t b = doc[i];
        switch (b) {
            default: {
                if (maxcol <= current + wordp + 1) {
                    if (!current) {
                        fwrite(wordbuf, wordp, 1, stderr);
                        wordp = 0;
                    }
                    fprintf(stderr, "\n    ");
                    gbl_lines_below++;
                    current = 0;
                }
                wordbuf[wordp++] = b;
                break;
            }
            case '\t': {
                if (maxcol <= current + wordp + 2) {
                    if (!current) {
                        fwrite(wordbuf, wordp, 1, stderr);
                        wordp = 0;
                    }
                    fprintf(stderr, "\n    ");
                    gbl_lines_below++;
                    current = 0;
                }
                wordbuf[wordp++] = ' ';
                wordbuf[wordp++] = ' ';
                break;
            }
            case '\n':
            case ' ': {
                if (wordp) {
                    int32_t oldcur = current;
                    int spacer = maxcol > current + wordp + 1;
                    if (spacer) current++;
                    else current = 0;
                    current += wordp;
                    if (oldcur) fprintf(stderr, spacer ? " " : "\n    ");
                    if (oldcur && !spacer) gbl_lines_below++;
                    fwrite(wordbuf, wordp, 1, stderr);
                    wordp = 0;
                }
                if (b == '\n') {
                    fprintf(stderr, "\n    ");
                    gbl_lines_below++;
                    current = 0;
                }
            }
        }
    }
    if (wordp) {
        int32_t oldcur = current;
        int spacer = maxcol > current + wordp + 1;
        if (spacer) current++;
        else current = 0;
        current += wordp + 1;
        if (oldcur) fprintf(stderr, spacer ? " " : "\n    ");
        if (oldcur && !spacer) gbl_lines_below++;
        fwrite(wordbuf, wordp, 1, stderr);
        wordp = 0;
    }
}

static void find_matches(JanetByteView prefix) {
    JanetTable *env = gbl_complete_env;
    gbl_match_count = 0;
    while (NULL != env) {
        JanetKV *kvend = env->data + env->capacity;
        for (JanetKV *kv = env->data; kv < kvend; kv++) {
            if (!janet_checktype(kv->key, JANET_SYMBOL)) continue;
            const uint8_t *sym = janet_unwrap_symbol(kv->key);
            check_match(prefix, sym, janet_string_length(sym));
        }
        env = env->proto;
    }
}

static void kshowdoc(void) {
    if (!gbl_complete_env) return;
    while (is_symbol_char_gen(gbl_buf[gbl_pos])) gbl_pos++;
    JanetByteView prefix = get_symprefix();
    Janet symbol = janet_symbolv(prefix.bytes, prefix.len);
    Janet entry = janet_table_get(gbl_complete_env, symbol);
    if (!janet_checktype(entry, JANET_TABLE)) return;
    Janet doc = janet_table_get(janet_unwrap_table(entry), janet_ckeywordv("doc"));
    if (!janet_checktype(doc, JANET_STRING)) return;
    JanetString docs = janet_unwrap_string(doc);
    int num_cols = getcols();
    clearlines();
    fprintf(stderr, "\n\n\n");
    gbl_lines_below += 3;
    resolve_format(janet_unwrap_table(entry));
    fprintf(stderr, "\n");
    gbl_lines_below += 1;
    doc_format(docs, num_cols);
    fprintf(stderr, "\n\n");
    gbl_lines_below += 2;
    /* Go up to original line (zsh-like autocompletion) */
    fprintf(stderr, "\x1B[%dA", gbl_lines_below);
    fflush(stderr);
}

static void kshowcomp(void) {
    JanetTable *env = gbl_complete_env;
    if (env == NULL) {
        insert(' ', 0);
        insert(' ', 0);
        return;
    }

    /* Advance while on symbol char */
    while (is_symbol_char_gen(gbl_buf[gbl_pos]))
        gbl_pos++;

    JanetByteView prefix = get_symprefix();
    if (prefix.len == 0) return;

    find_matches(prefix);

    check_specials(prefix);

    JanetByteView lcp = longest_common_prefix();
    for (int i = prefix.len; i < lcp.len; i++) {
        insert(lcp.bytes[i], 0);
    }

    if (!gbl_lines_below && prefix.len != lcp.len) return;

    int32_t maxlen = 0;
    for (int i = 0; i < gbl_match_count; i++)
        if (gbl_matches[i].len > maxlen)
            maxlen = gbl_matches[i].len;

    int num_cols = getcols();
    clearlines();
    if (gbl_match_count >= 2) {

        /* Second pass, print */
        int col_width = maxlen + 4;
        int cols = num_cols / col_width;
        if (cols == 0) cols = 1;
        int current_col = 0;
        for (int i = 0; i < gbl_match_count; i++) {
            if (current_col == 0) {
                putc('\n', stderr);
                gbl_lines_below++;
            }
            JanetByteView s = gbl_matches[i];
            fprintf(stderr, "%s", (const char *) s.bytes);
            for (int j = s.len; j < col_width; j++) {
                putc(' ', stderr);
            }
            current_col = (current_col + 1) % cols;
        }

        /* Go up to original line (zsh-like autocompletion) */
        fprintf(stderr, "\x1B[%dA", gbl_lines_below);

        fflush(stderr);
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

    if (write_console((char *) gbl_prompt, gbl_plen) == -1) return -1;
    for (;;) {
        char c;
        char seq[3];

        int rc;
        do {
            rc = read_console(&c, 1);
        } while (rc < 0 && errno == EINTR);
        if (rc <= 0) return -1;

        switch (c) {
            default:
                if ((unsigned char) c < 0x20) break;
                if (insert(c, 1)) return -1;
                break;
            case 1:     /* ctrl-a */
                gbl_pos = 0;
                refresh();
                break;
            case 2:     /* ctrl-b */
                kleft();
                break;
            case 3:     /* ctrl-c */
                clearlines();
                norawmode();
#ifdef _WIN32
                ExitProcess(1);
#else
                kill(getpid(), SIGINT);
#endif
            /* fallthrough */
            case 17:    /* ctrl-q */
                gbl_cancel_current_repl_form = 1;
                clearlines();
                return -1;
            case 4:     /* ctrl-d, eof */
                if (gbl_len == 0) {   /* quit on empty line */
                    clearlines();
                    return -1;
                }
                kdelete(1);
                break;
            case 5:     /* ctrl-e */
                gbl_pos = gbl_len;
                refresh();
                break;
            case 6:     /* ctrl-f */
                kright();
                break;
            case 7: /* ctrl-g */
                kshowdoc();
                refresh();
                break;
            case 127:   /* backspace */
            case 8:     /* ctrl-h */
                kbackspace(1);
                break;
            case 9:     /* tab */
                kshowcomp();
                refresh();
                break;
            case 11: /* ctrl-k */
                gbl_buf[gbl_pos] = '\0';
                gbl_len = gbl_pos;
                refresh();
                break;
            case 12:    /* ctrl-l */
                clear();
                refresh();
                break;
            case 13:    /* enter */
                clearlines();
                return 0;
            case 14: /* ctrl-n */
                historymove(-1);
                break;
            case 16: /* ctrl-p */
                historymove(1);
                break;
            case 21: { /* ctrl-u */
                memmove(gbl_buf, gbl_buf + gbl_pos, gbl_len - gbl_pos);
                gbl_len -= gbl_pos;
                gbl_buf[gbl_len] = '\0';
                gbl_pos = 0;
                refresh();
                break;
            }
            case 23: /* ctrl-w */
                kbackspacew();
                break;
#ifndef _WIN32
            case 26: /* ctrl-z */
                clearlines();
                norawmode();
                kill(getpid(), SIGSTOP);
                rawmode();
                refresh();
                break;
#endif
            case 27:    /* escape sequence */
                /* Read the next two bytes representing the escape sequence.
                 * Use two calls to handle slow terminals returning the two
                 * chars at different times. */
                if (read_console(seq, 1) == -1) break;
                /* Esc[ = Control Sequence Introducer (CSI) */
                if (seq[0] == '[') {
                    if (read_console(seq + 1, 1) == -1) break;
                    if (seq[1] >= '0' && seq[1] <= '9') {
                        /* Extended escape, read additional byte. */
                        if (read_console(seq + 2, 1) == -1) break;
                        if (seq[2] == '~') {
                            switch (seq[1]) {
                                case '1': /* Home */
                                    gbl_pos = 0;
                                    refresh();
                                    break;
                                case '3': /* delete */
                                    kdelete(1);
                                    break;
                                case '4': /* End */
                                    gbl_pos = gbl_len;
                                    refresh();
                                    break;
                                default:
                                    break;
                            }
                        }
                    } else if (seq[0] == 'O') {
                        if (read_console(seq + 1, 1) == -1) break;
                        switch (seq[1]) {
                            default:
                                break;
                            case 'H': /* Home (some keyboards) */
                                gbl_pos = 0;
                                refresh();
                                break;
                            case 'F': /* End (some keyboards) */
                                gbl_pos = gbl_len;
                                refresh();
                                break;
                        }
                    } else {
                        switch (seq[1]) {
                            /* Single escape sequences */
                            default:
                                break;
                            case 'A': /* Up */
                                historymove(1);
                                break;
                            case 'B': /* Down */
                                historymove(-1);
                                break;
                            case 'C': /* Right */
                                kright();
                                break;
                            case 'D': /* Left */
                                kleft();
                                break;
                            case 'H': /* Home */
                                gbl_pos = 0;
                                refresh();
                                break;
                            case 'F': /* End */
                                gbl_pos = gbl_len;
                                refresh();
                                break;
                        }
                    }
                } else {
                    /* Check alt-(shift) bindings */
                    switch (seq[0]) {
                        default:
                            break;
                        case 'd': /* Alt-d */
                            kdeletew();
                            break;
                        case 'b': /* Alt-b */
                            kleftw();
                            break;
                        case 'f': /* Alt-f */
                            krightw();
                            break;
                        case ',': /* Alt-, */
                            historymove(JANET_HISTORY_MAX);
                            break;
                        case '.': /* Alt-. */
                            historymove(-JANET_HISTORY_MAX);
                            break;
                        case 127: /* Alt-backspace */
                            kbackspacew();
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
        janet_free(gbl_history[i]);
    gbl_historyi = 0;
}

void janet_line_get(const char *p, JanetBuffer *buffer) {
    gbl_prompt = p;
    buffer->count = 0;
    gbl_historyi = 0;
    if (check_simpleline(buffer)) return;
    FILE *out = janet_dynfile("err", stderr);
    if (line()) {
        norawmode();
        fputc('\n', out);
        return;
    }
    fflush(stdin);
    norawmode();
    fputc('\n', out);
    janet_buffer_ensure(buffer, gbl_len + 1, 2);
    memcpy(buffer->data, gbl_buf, gbl_len);
    buffer->data[gbl_len] = '\n';
    buffer->count = gbl_len + 1;
    replacehistory();
}

static void clear_at_exit(void) {
    if (!gbl_israwmode) {
        clearlines();
        norawmode();
    }
}

#endif

/*
 * Entry
 */

int main(int argc, char **argv) {
    int i, status;
    JanetArray *args;
    JanetTable *env;

#ifdef _WIN32
    setup_console_output();
#endif

#if !defined(JANET_SIMPLE_GETLINE)
    atexit(clear_at_exit);
#endif

#if defined(JANET_PRF)
    uint8_t hash_key[JANET_HASH_KEY_SIZE + 1];
#ifdef JANET_REDUCED_OS
    char *envvar = NULL;
#else
    char *envvar = getenv("JANET_HASHSEED");
#endif
    if (NULL != envvar) {
        strncpy((char *) hash_key, envvar, sizeof(hash_key) - 1);
    } else if (janet_cryptorand(hash_key, JANET_HASH_KEY_SIZE) != 0) {
        fputs("unable to initialize janet PRF hash function.\n", stderr);
        return 1;
    }
    janet_init_hash_key(hash_key);
#endif

    /* Set up VM */
    janet_init();

    /* Replace original getline with new line getter */
    JanetTable *replacements = janet_table(0);
    janet_table_put(replacements, janet_csymbolv("getline"), janet_wrap_cfunction(janet_line_getter));
    janet_line_init();

    /* Get core env */
    env = janet_core_env(replacements);

    /* Create args tuple */
    args = janet_array(argc);
    for (i = 1; i < argc; i++)
        janet_array_push(args, janet_cstringv(argv[i]));

    /* Save current executable path to (dyn :executable) */
    janet_table_put(env, janet_ckeywordv("executable"), janet_cstringv(argv[0]));

    /* Run startup script */
    Janet mainfun;
    janet_resolve(env, janet_csymbol("cli-main"), &mainfun);
    Janet mainargs[1] = { janet_wrap_array(args) };
    JanetFiber *fiber = janet_fiber(janet_unwrap_function(mainfun), 64, 1, mainargs);
    fiber->env = env;

    /* Run the fiber in an event loop */
    status = janet_loop_fiber(fiber);

    /* Deinitialize vm */
    janet_deinit();
    janet_line_deinit();

    return status;
}
