#include "unit.h"
#include <dst/dst.h>

int main() {
    dst_init();
    DstTable *table = dst_table(10);
    assert(table->count == 0);
    dst_table_put(table, dst_cstringv("a"), dst_cstringv("b"));
    dst_table_put(table, dst_cstringv("b"), dst_cstringv("a"));
    dst_table_put(table, dst_cstringv("a"), dst_cstringv("c"));
    assert(table->count == 2);
    dst_table_remove(table, dst_cstringv("a"));
    assert(table->count == 1);
    return 0;
}
