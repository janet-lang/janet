#include <dst/dst.h>

#ifdef DST_LIB
#define dst_entry _dst_init
#else
#define dst_entry dst_testlib_init
#endif

int dst_entry(DstArgs args) {
    DstTable *module = dst_get_module(args);
    dst_module_def(module, "pi", dst_wrap_real(M_PI));
    *args.ret = dst_wrap_table(module);
    return 0;
}
