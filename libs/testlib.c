#include <dst/dst.h>

static void additem(DstTable *t, const char *name, Dst val) {
    DstTable *subt = dst_table(1);
    dst_table_put(subt, dst_csymbolv("value"), val);
    dst_table_put(t, dst_csymbolv(name), dst_wrap_table(subt));
}

int _dst_init(int32_t argn, Dst *argv, Dst *ret) {
    DstTable *table;
    if (argn >= 2 && dst_checktype(argv[1], DST_TABLE)) {
        table = dst_unwrap_table(argv[1]);
    } else {
        table = dst_table(0);
    }
    additem(table, "pi", dst_wrap_real(M_PI));
    *ret = dst_wrap_table(table);
    return 0;
}
