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

#include "sqlite3.c"

#include "sqlite3.h"

#include <dst/dst.h>

/* Called to garbage collect a sqlite3 connection */
static int gcsqlite(void *p, size_t s) {
    (void) s;
    sqlite3 *conn = *((sqlite3 **)p);
    sqlite3_close_v2(conn);
    return 0;
}

static const DstAbstractType sql_conn_type = {
    ":sqlite3.connection",
    gcsqlite,
    NULL,
};

/* Open a new database connection */
static int sql_open(DstArgs args) {
    sqlite3 **conn;
    const uint8_t *filename;
    int status;
    dst_fixarity(args, 1);
    dst_arg_string(filename, args, 0);
    conn = (sqlite3 **) dst_abstract(&sql_conn_type, sizeof(sqlite3 *));
    status = sqlite3_open((const char *)filename, conn);
    if (status == SQLITE_OK) {
        return dst_return(args, dst_wrap_abstract(conn));
    } else {
        const char *err = sqlite3_errmsg(*conn);
        return dst_throw(args, err);
    }
}

static int sql_close(DstArgs args) {
    sqlite3 **conn;
    int status;
    dst_fixarity(args, 1);
    dst_checkabstract(args, 0, &sql_conn_type);
    conn = (sqlite3 **)dst_unwrap_abstract(args.v[0]);
    status = sqlite3_close_v2(*conn);
    if (status == SQLITE_OK) {
        return dst_return(args, dst_wrap_nil());
    } else {
        return dst_throw(args, "unable to close the sqlite3 connection");
    }
}

static int sql_execute_callback(void *rowsp, int n, char **vals, char **colnames) {
    int i;
    DstArray *rows = (DstArray *)rowsp;
    DstKV *row = dst_struct_begin(n);
    for (i = 0; i < n; i++) {
        dst_struct_put(row, dst_cstringv(colnames[i]), dst_cstringv(vals[i]));
    }
    dst_array_push(rows, dst_wrap_struct(dst_struct_end(row)));
    return 0;
}

static int sql_sql(DstArgs args) {
    int status;
    char *errmsg = "connection closed";
    const uint8_t *str;
    DstArray *rows;
    sqlite3 **conn;
    dst_fixarity(args, 2);
    dst_checkabstract(args, 0, &sql_conn_type);
    conn = (sqlite3 **)dst_unwrap_abstract(args.v[0]);
    dst_arg_string(str, args, 1);
    rows = dst_array(10);
    status = sqlite3_exec(
            *conn,
            (const char *)str,
            sql_execute_callback,
            rows,
            &errmsg);
    if (status == SQLITE_OK) {
        return dst_return(args, dst_wrap_array(rows));
    } else {
        return dst_throw(args, errmsg);
    }
}

/*
static int sql_tables(DstArgs args) {}
static int sql_changes(DstArgs args) {}
static int sql_timeout(DstArgs args) {}
static int sql_rowid(DstArgs args) {}
*/

/*****************************************************************************/

static const DstReg cfuns[] = {
    {"open", sql_open},
    {"close", sql_close},
    {"sql", sql_sql},
    /*{"tables", sql_tables},*/
    /*{"changes", sql_changes},*/
    /*{"timeout", sql_timeout},*/
    /*{"rowid", sql_rowid},*/
    {NULL, NULL}
};

int _dst_init(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}
