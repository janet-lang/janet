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

#include "sqlite3.h"
#include <janet/janet.h>

#define FLAG_CLOSED 1

#define MSG_DB_CLOSED "database already closed"

typedef struct {
    sqlite3* handle;
    int flags;
} Db;

/* Close a db, noop if already closed */
static void closedb(Db *db) {
    if (!(db->flags & FLAG_CLOSED)) {
        db->flags |= FLAG_CLOSED;
        sqlite3_close_v2(db->handle);
    }
}

/* Called to garbage collect a sqlite3 connection */
static int gcsqlite(void *p, size_t s) {
    (void) s;
    Db *db = (Db *)p;
    closedb(db);
    return 0;
}

static const JanetAbstractType sql_conn_type = {
    ":sqlite3.connection",
    gcsqlite,
    NULL,
};

/* Open a new database connection */
static int sql_open(JanetArgs args) {
    sqlite3 *conn;
    const uint8_t *filename;
    int status;
    JANET_FIXARITY(args, 1);
    JANET_ARG_STRING(filename, args, 0);
    status = sqlite3_open((const char *)filename, &conn);
    if (status == SQLITE_OK) {
        Db *db = (Db *) janet_abstract(&sql_conn_type, sizeof(Db));
        db->handle = conn;
        db->flags = 0;
        JANET_RETURN_ABSTRACT(args, db);
    } else {
        const char *err = sqlite3_errmsg(conn);
        JANET_THROW(args, err);
    }
}

/* Close a database connection */
static int sql_close(JanetArgs args) {
    Db *db;
    JANET_FIXARITY(args, 1);
    JANET_CHECKABSTRACT(args, 0, &sql_conn_type);
    db = (Db *)janet_unwrap_abstract(args.v[0]);
    closedb(db);
    JANET_RETURN_NIL(args);
}

/* Check for embedded NULL bytes */
static int has_null(const uint8_t *str, int32_t len) {
    while (len--) {
        if (!str[len])
            return 1;
    }
    return 0;
}

/* Bind a single parameter */
static const char *bind1(sqlite3_stmt *stmt, int index, Janet value) {
    int res;
    switch (janet_type(value)) {
        default:
            return "invalid sql value";
        case JANET_NIL:
            res = sqlite3_bind_null(stmt, index);
            break;
        case JANET_FALSE:
            res = sqlite3_bind_int(stmt, index, 0);
            break;
        case JANET_TRUE:
            res = sqlite3_bind_int(stmt, index, 1);
            break;
        case JANET_REAL:
            res = sqlite3_bind_double(stmt, index, janet_unwrap_real(value));
            break;
        case JANET_INTEGER:
            res = sqlite3_bind_int64(stmt, index, janet_unwrap_integer(value));
            break;
        case JANET_STRING:
        case JANET_SYMBOL:
            {
                const uint8_t *str = janet_unwrap_string(value);
                int32_t len = janet_string_length(str);
                if (has_null(str, len)) {
                    return "cannot have embedded nulls in text values";
                } else {
                    res = sqlite3_bind_text(stmt, index, (const char *)str, len + 1, SQLITE_STATIC);
                }
            }
            break;
        case JANET_BUFFER:
            {
                JanetBuffer *buffer = janet_unwrap_buffer(value);
                res = sqlite3_bind_blob(stmt, index, buffer->data, buffer->count, SQLITE_STATIC);
            }
            break;
    }
    if (res != SQLITE_OK) {
        sqlite3 *db = sqlite3_db_handle(stmt);
        return sqlite3_errmsg(db);
    }
    return NULL;
}

/* Bind many parameters */
static const char *bindmany(sqlite3_stmt *stmt, Janet params) {
    /* parameters */
    const Janet *seq;
    const JanetKV *kvs;
    int32_t len, cap;
    int limitindex = sqlite3_bind_parameter_count(stmt);
    if (janet_indexed_view(params, &seq, &len)) {
        if (len > limitindex + 1) {
            return "invalid index in sql parameters";
        }
        for (int i = 0; i < len; i++) {
            const char *err = bind1(stmt, i + 1, seq[i]);
            if (err) {
                return err;
            }
        }
    } else if (janet_dictionary_view(params, &kvs, &len, &cap)) {
        for (int i = 0; i < cap; i++) {
            int index = 0;
            switch (janet_type(kvs[i].key)) {
                default:
                    /* Will fail */
                    break;
                case JANET_NIL:
                    /* Will skip as nil keys indicate empty hash table slot */
                    continue;
                case JANET_INTEGER:
                    index = janet_unwrap_integer(kvs[i].key);
                    break;
                case JANET_STRING:
                case JANET_SYMBOL:
                    {
                        const uint8_t *s = janet_unwrap_string(kvs[i].key);
                        index = sqlite3_bind_parameter_index(
                                stmt,
                                (const char *)s);
                    }
                    break;
            }
            if (index <= 0 || index > limitindex) {
                return "invalid index in sql parameters";
            }
            const char *err = bind1(stmt, index, kvs[i].value);
            if (err) {
                return err;
            }
        }
    } else {
        return "invalid type for sql parameters";
    }
    return NULL;
}

/* Execute a statement but don't collect results */
static const char *execute(sqlite3_stmt *stmt) {
    int status;
    const char *ret = NULL;
    do {
        status = sqlite3_step(stmt);
    } while (status == SQLITE_ROW);
    /* Check for errors */
    if (status != SQLITE_DONE) {
        sqlite3 *db = sqlite3_db_handle(stmt);
        ret = sqlite3_errmsg(db);
    }
    return ret;
}

/* Execute and return values from prepared statement */
static const char *execute_collect(sqlite3_stmt *stmt, JanetArray *rows) {
    /* Count number of columns in result */
    int ncol = sqlite3_column_count(stmt);
    int status;
    const char *ret = NULL;

    /* Get column names */
    Janet *tupstart = janet_tuple_begin(ncol);
    for (int i = 0; i < ncol; i++) {
        tupstart[i] = janet_cstringv(sqlite3_column_name(stmt, i));
    }
    const Janet *colnames = janet_tuple_end(tupstart);

    do {
        status = sqlite3_step(stmt);
        if (status == SQLITE_ROW) {
            JanetKV *row = janet_struct_begin(ncol);
            for (int i = 0; i < ncol; i++) {
                int t = sqlite3_column_type(stmt, i);
                Janet value;
                switch (t) {
                    case SQLITE_NULL:
                        value = janet_wrap_nil();
                        break;
                    case SQLITE_INTEGER:
                        value = janet_wrap_integer(sqlite3_column_int(stmt, i));
                        break;
                    case SQLITE_FLOAT:
                        value = janet_wrap_real(sqlite3_column_double(stmt, i));
                        break;
                    case SQLITE_TEXT:
                        {
                            int nbytes = sqlite3_column_bytes(stmt, i) - 1;
                            uint8_t *str = janet_string_begin(nbytes);
                            memcpy(str, sqlite3_column_text(stmt, i), nbytes);
                            value = janet_wrap_string(janet_string_end(str));
                        }
                        break;
                    case SQLITE_BLOB:
                        {
                            int nbytes = sqlite3_column_bytes(stmt, i);
                            JanetBuffer *b = janet_buffer(nbytes);
                            memcpy(b->data, sqlite3_column_blob(stmt, i), nbytes);
                            b->count = nbytes;
                            value = janet_wrap_buffer(b);
                        }
                        break;
                }
                janet_struct_put(row, colnames[i], value);
            }
            janet_array_push(rows, janet_wrap_struct(janet_struct_end(row)));
        }
    } while (status == SQLITE_ROW);

    /* Check for errors */
    if (status != SQLITE_DONE) {
        sqlite3 *db = sqlite3_db_handle(stmt);
        ret = sqlite3_errmsg(db);
    }
    return ret;
}

/* Evaluate a string of sql */
static int sql_eval(JanetArgs args) {
    const char *err;
    sqlite3_stmt *stmt = NULL, *stmt_next = NULL;
    const uint8_t *query;

    JANET_MINARITY(args, 2);
    JANET_MAXARITY(args, 3);
    JANET_CHECKABSTRACT(args, 0, &sql_conn_type);
    Db *db = (Db *)janet_unwrap_abstract(args.v[0]);
    if (db->flags & FLAG_CLOSED) {
        JANET_THROW(args, MSG_DB_CLOSED);
    }
    JANET_ARG_STRING(query, args, 1);
    if (has_null(query, janet_string_length(query))) {
        err = "cannot have embedded NULL in sql statememts";
        goto error;
    }
    JanetArray *rows = janet_array(10);
    const char *c = (const char *)query;

    /* Evaluate all statements in a loop */
    do {
        /* Compile the next statement */
        if (sqlite3_prepare_v2(db->handle, c, -1, &stmt_next, &c) != SQLITE_OK) {
            err = sqlite3_errmsg(db->handle);
            goto error;
        }
        /* Check if we have found last statement */
        if (NULL == stmt_next) {
            /* Execute current statement and collect results */
            if (stmt) {
                err = execute_collect(stmt, rows);
                if (err) goto error;
            }
        } else {
            /* Execute current statement but don't collect results. */
            if (stmt) {
                err = execute(stmt);
                if (err) goto error;
            }
            /* Bind params to next statement*/
            if (args.n == 3) {
                /* parameters */
                err = bindmany(stmt_next, args.v[2]);
                if (err) goto error;
            }
        }
        /* rotate stmt and stmt_next */
        if (stmt) sqlite3_finalize(stmt);
        stmt = stmt_next;
        stmt_next = NULL;
    } while (NULL != stmt);

    /* Good return path */
    JANET_RETURN_ARRAY(args, rows);

error:
    if (stmt) sqlite3_finalize(stmt);
    if (stmt_next) sqlite3_finalize(stmt_next);
    JANET_THROW(args, err);
}

/* Convert int64_t to a string */
static const uint8_t *coerce_int64(int64_t x) {
    uint8_t bytes[40];
    int i = 0;
    /* Edge cases */
    if (x == 0) return janet_cstring("0");
    if (x == INT64_MIN) return janet_cstring("-9,223,372,036,854,775,808");
    /* Negative becomes pos */
    if (x < 0) {
        bytes[i++] = '-';
        x = -x;
    }
    while (x) {
        bytes[i++] = x % 10;
        x = x / 10;
    }
    bytes[i] = '\0';
    return janet_string(bytes, i);
}

/* Gets the last inserted row id */
static int sql_last_insert_rowid(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    JANET_CHECKABSTRACT(args, 0, &sql_conn_type);
    Db *db = (Db *)janet_unwrap_abstract(args.v[0]);
    if (db->flags & FLAG_CLOSED) {
        JANET_THROW(args, MSG_DB_CLOSED);
    }
    sqlite3_int64 id = sqlite3_last_insert_rowid(db->handle);
    if (id >= INT32_MIN && id <= INT32_MAX) {
        JANET_RETURN_INTEGER(args, (int32_t) id);
    }
    /* Convert to string */
    JANET_RETURN_STRING(args, coerce_int64(id));
}

/* Get the sqlite3 errcode */
static int sql_error_code(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    JANET_CHECKABSTRACT(args, 0, &sql_conn_type);
    Db *db = (Db *)janet_unwrap_abstract(args.v[0]);
    if (db->flags & FLAG_CLOSED) {
        JANET_THROW(args, MSG_DB_CLOSED);
    }
    int errcode = sqlite3_errcode(db->handle);
    JANET_RETURN_INTEGER(args, errcode);
}

/*****************************************************************************/

static const JanetReg cfuns[] = {
    {"open", sql_open},
    {"close", sql_close},
    {"eval", sql_eval},
    {"last-insert-rowid", sql_last_insert_rowid},
    {"error-code", sql_error_code},
    {NULL, NULL}
};

JANET_MODULE_ENTRY(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, "sqlite3", cfuns);
    return 0;
}
