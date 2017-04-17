#include <gst/gst.h>

/* Wrapper functions wrap a data type that is used from C into a
 * gst value, which can then be used in gst. */

GstValue gst_wrap_nil() {
    GstValue y;
    y.type = GST_NIL;
    return y;
}

#define GST_WRAP_DEFINE(NAME, TYPE, GTYPE, UM)\
GstValue gst_wrap_##NAME(TYPE x) {\
    GstValue y;\
    y.type = GTYPE;\
    y.data.UM = x;\
    return y;\
}

GST_WRAP_DEFINE(number, GstNumber, GST_NUMBER, number)
GST_WRAP_DEFINE(boolean, int, GST_BOOLEAN, boolean)
GST_WRAP_DEFINE(string, const uint8_t *, GST_STRING, string)
GST_WRAP_DEFINE(array, GstArray *, GST_ARRAY, array)
GST_WRAP_DEFINE(tuple, const GstValue *, GST_TUPLE, tuple)
GST_WRAP_DEFINE(struct, const GstValue *, GST_STRUCT, st)
GST_WRAP_DEFINE(thread, GstThread *, GST_THREAD, thread)
GST_WRAP_DEFINE(buffer, GstBuffer *, GST_BYTEBUFFER, buffer)
GST_WRAP_DEFINE(function, GstFunction *, GST_FUNCTION, function)
GST_WRAP_DEFINE(cfunction, GstCFunction, GST_CFUNCTION, cfunction)
GST_WRAP_DEFINE(object, GstObject *, GST_OBJECT, object)
GST_WRAP_DEFINE(userdata, void *, GST_USERDATA, pointer)
GST_WRAP_DEFINE(funcenv, GstFuncEnv *, GST_FUNCENV, env)
GST_WRAP_DEFINE(funcdef, GstFuncDef *, GST_FUNCDEF, def)

#undef GST_WRAP_DEFINE

GstValue gst_c_module_object(Gst *vm, const GstModuleItem *mod) {
    GstObject *module = gst_object(vm, 10);
    while (mod->name != NULL) {
        GstValue key = gst_string_cv(vm, mod->name);
        gst_object_put(vm, module, key, gst_wrap_cfunction(mod->data));
        mod++;
    }
    return gst_wrap_object(module);
}

GstValue gst_c_module_struct(Gst *vm, const GstModuleItem *mod) {
    uint32_t count = 0;
    const GstModuleItem *m = mod;
    GstValue *st;
    while (m->name != NULL) {
        ++count;
        ++m;
    }
    st = gst_struct_begin(vm, count);
    m = mod;
    while (m->name != NULL) {
        gst_struct_put(st,
                gst_string_cv(vm, m->name),
                gst_wrap_cfunction(m->data));
        ++m;
    }
    return gst_wrap_struct(gst_struct_end(vm, st));
    
}

void gst_c_register(Gst *vm, const char *packagename, GstValue mod) {
    if (vm->rootenv == NULL)
        vm->rootenv = gst_object(vm, 10);
    gst_object_put(vm, vm->rootenv, gst_string_cv(vm, packagename), mod);
}
