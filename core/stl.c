#include <gst/gst.h>
#include <gst/parse.h>
#include <gst/compile.h>
#include <gst/stl.h>

/****/
/* Core */
/****/

/* Print values for inspection */
int gst_stl_print(Gst *vm) {
    uint32_t j, count;
    count = gst_count_args(vm);
    for (j = 0; j < count; ++j) {
        uint32_t i;
        const uint8_t *string = gst_to_string(vm, gst_arg(vm, j));
        uint32_t len = gst_string_length(string);
        for (i = 0; i < len; ++i)
            fputc(string[i], stdout);
        fputc('\n', stdout);
    }
    return GST_RETURN_OK;
}

/* Get class value */
int gst_stl_getclass(Gst *vm) {
    GstValue class = gst_get_class(gst_arg(vm, 0));
    gst_c_return(vm, class);
}

/* Set class value */
int gst_stl_setclass(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    GstValue class = gst_arg(vm, 1);
    const char *err = gst_set_class(x, class);
    if (err != NULL)
        gst_c_throwc(vm, err);
    gst_c_return(vm, x);
}

/* Create a buffer */
int gst_stl_make_buffer(Gst *vm) {
    uint32_t i, count;
    GstValue buf;
    buf.type = GST_BYTEBUFFER;
    buf.data.buffer = gst_buffer(vm, 10);
    count = gst_count_args(vm);
    for (i = 0; i < count; ++i) {
        const uint8_t *string = gst_to_string(vm, gst_arg(vm, i));
        gst_buffer_append(vm, buf.data.buffer, string, gst_string_length(string));
    }
    gst_c_return(vm, buf);
}

/* To string */
int gst_stl_tostring(Gst *vm) {
    GstValue ret;
    const uint8_t *string = gst_to_string(vm, gst_arg(vm, 0));
    ret.type = GST_STRING;
    ret.data.string = string;
    gst_c_return(vm, ret);
}

/* Exit */
int gst_stl_exit(Gst *vm) {
    int ret;
    GstValue exitValue = gst_arg(vm, 0);
    ret = (exitValue.type == GST_NUMBER) ? exitValue.data.number : 0;
    exit(ret);
    return GST_RETURN_OK;
}

/* Load core */
void gst_stl_load_core(GstCompiler *c) {
    gst_compiler_add_global_cfunction(c, "print", gst_stl_print);
    gst_compiler_add_global_cfunction(c, "get-class", gst_stl_getclass);
    gst_compiler_add_global_cfunction(c, "set-class", gst_stl_setclass);
    gst_compiler_add_global_cfunction(c, "make-buffer", gst_stl_make_buffer);
    gst_compiler_add_global_cfunction(c, "tostring", gst_stl_tostring);
    gst_compiler_add_global_cfunction(c, "exit", gst_stl_exit);
}

/****/
/* Parsing */
/****/

/* Get an integer power of 10 */
static double exp10(int power) {
    if (power == 0) return 1;
    if (power > 0) {
        double result = 10;
        int currentPower = 1;
        while (currentPower * 2 <= power) {
            result = result * result;
            currentPower *= 2;
        }
        return result * exp10(power - currentPower);
    } else {
        return 1 / exp10(-power);
    }
}

/* Read a number from a string. Returns if successfuly
 * parsed a number from the enitre input string.
 * If returned 1, output is int ret.*/
static int read_number(const uint8_t *string, const uint8_t *end, double *ret, int forceInt) {
    int sign = 1, x = 0;
    double accum = 0, exp = 1, place = 1;
    /* Check the sign */
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        if (*string == '.' && !forceInt) {
            place = 0.1;
        } else if (!forceInt && (*string == 'e' || *string == 'E')) {
            /* Read the exponent */
            ++string;
            if (string >= end) return 0;
            if (!read_number(string, end, &exp, 1))
                return 0;
            exp = exp10(exp);
            break;
        } else {
            x = *string;
            if (x < '0' || x > '9') return 0;
            x -= '0';
            if (place < 1) {
                accum += x * place;
                place *= 0.1;
            } else {
                accum *= 10;
                accum += x;
            }
        }
        ++string;
    }
    *ret = accum * sign * exp;
    return 1;
}

/* Convert string to integer */
int gst_stl_parse_number(Gst *vm) {
    GstValue ret;
    double number;
    const uint8_t *str = gst_to_string(vm, gst_arg(vm, 0));
    const uint8_t *end = str + gst_string_length(str);
    if (read_number(str, end, &number, 0)) {
        ret.type = GST_NUMBER;
        ret.data.number = number;
    } else {
        ret.type = GST_NIL;
    }
    gst_c_return(vm, ret);

}

/* Parse a source string into an AST */
int gst_stl_parse(Gst *vm) {
    const uint8_t *source = gst_to_string(vm, gst_arg(vm, 0));
    GstParser p;
    /* init state */
    gst_parser(&p, vm);

    /* Get and parse input until we have a full form */
    gst_parse_string(&p, source);
    if (p.status == GST_PARSER_PENDING) {
        gst_c_throwc(vm, "incomplete source");
    } else if (p.status == GST_PARSER_ERROR) {
        gst_c_throwc(vm, p.error);
    } else {
        gst_c_return(vm, p.value);
    }
}

/* Load parsing */
void gst_stl_load_parse(GstCompiler *c) {
    gst_compiler_add_global_cfunction(c, "parse", gst_stl_parse);
    gst_compiler_add_global_cfunction(c, "parse-number", gst_stl_parse_number);
}

/****/
/* Compiling */
/****/

/* Compile an ast */
int gst_stl_compile(Gst *vm) {
    GstValue ast = gst_arg(vm, 0);
    GstValue env = gst_arg(vm, 1);
    GstValue ret;
    GstCompiler c;
    /* init state */
    if (env.type == GST_NIL) {
        env = vm->rootenv;
    }
    gst_compiler(&c, vm);
    gst_compiler_env(&c, env);
    /* Prepare return value */
    ret.type = GST_FUNCTION;
    ret.data.function = gst_compiler_compile(&c, ast);
    /* Check for errors */
    if (c.error == NULL) {
        gst_c_return(vm, ret);
    } else {
        gst_c_throwc(vm, c.error);
    }
}

/* Load compilation */
void gst_stl_load_compile(GstCompiler *c) {
    gst_compiler_add_global_cfunction(c, "compile", gst_stl_compile);
}

/****/
/* Serialization */
/****/

/* Serialize data into buffer */
int gst_stl_serialize(Gst *vm) {
    const char *err;
    uint32_t i;
    GstValue buffer = gst_arg(vm, 0);
    if (buffer.type != GST_BYTEBUFFER)
        gst_c_throwc(vm, "expected buffer");
    for (i = 1; i < gst_count_args(vm); ++i) {
        err = gst_serialize(vm, buffer.data.buffer, gst_arg(vm, i));
        if (err != NULL)
            gst_c_throwc(vm, err);
    }
    gst_c_return(vm, buffer);
}

/* Load serilization */
void gst_stl_load_serialization(GstCompiler *c) {
    gst_compiler_add_global_cfunction(c, "serialize", gst_stl_serialize);
}

/* Read data from a linear sequence of memory */

/****/
/* IO */
/****/

/* TODO - add userdata to allow for manipulation of FILE pointers. */

/****/
/* Bootstraping */
/****/

/* Load all libraries */
void gst_stl_load(GstCompiler *c) {
    gst_stl_load_core(c);
    gst_stl_load_parse(c);
    gst_stl_load_compile(c);
    gst_stl_load_serialization(c);
}
