/* This implemets a standard library in gst. Some of this
 * will eventually be ported over to gst if possible */
#include "gst.h"

/****/
/* Misc */
/****/

/* Print values for inspection */
int print(Gst *vm) {
    uint32_t j, count;
    count = gst_count_args(vm);
    for (j = 0; j < count; ++j) {
        string_put(stdout, gst_to_string(vm, gst_arg(vm, j)));
        fputc('\n', stdout);
    }
    return GST_RETURN_OK;
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
    if (c.error != NULL) {
		gst_c_return(vm, ret);
    } else {
		gst_c_throwc(vm, c.error);
    }
}

/****/
/* IO */
/****/

/* TODO - add userdata to allow for manipulation of FILE pointers. */
