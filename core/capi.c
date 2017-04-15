#include <gst/gst.h>

GstObject *gst_c_module(Gst *vm, const GstModuleItem *mod) {
    GstObject *module = gst_object(vm, 10);
    while (mod->name != NULL) {
        GstValue key = gst_string_cv(vm, mod->name);
        GstValue val;
        val.type = GST_CFUNCTION;
        val.data.cfunction = mod->data;
        gst_object_put(vm, module, key, val);
        mod++;
    }
    return module;
}

void gst_c_register(Gst *vm, const char *packagename, GstObject *mod) {
    GstValue modv;
    if (vm->rootenv == NULL)
        vm->rootenv = gst_object(vm, 10);
    modv.type = GST_OBJECT;
    modv.data.object = mod;
    gst_object_put(vm, vm->rootenv, gst_string_cv(vm, packagename), modv);
}
