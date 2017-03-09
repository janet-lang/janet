/* This implemets a standard library in gst. Some of this
 * will eventually be ported over to gst if possible */
#include "stl.h"
#include "gst.h"

/****/
/* Core */
/****/

/* Print values for inspection */
int gst_stl_print(Gst *vm) {
    uint32_t j, count;
    count = gst_count_args(vm);
    for (j = 0; j < count; ++j) {
        uint32_t i;
        uint8_t *string = gst_to_string(vm, gst_arg(vm, j));
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

/* Load core */
void gst_stl_load_core(GstCompiler *c) {
    gst_compiler_add_global_cfunction(c, "print", gst_stl_print);
    gst_compiler_add_global_cfunction(c, "get-class", gst_stl_getclass);
    gst_compiler_add_global_cfunction(c, "set-class", gst_stl_setclass);
}

/****/
/* Parsing */
/****/

/* Parse a source string into an AST */
int gst_stl_parse(Gst *vm) {
	uint8_t *source = gst_to_string(vm, gst_arg(vm, 0));
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
    gst_compiler(&c, vm);
    /* Check for environment variables */
	if (env.type == GST_OBJECT) {
		/* Iterate through environment, adding globals */
  	} else if (env.type != GST_NIL) {
		gst_c_throwc(vm, "invalid type for environment");
	}
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
}
