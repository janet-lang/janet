#include "unit.h"
#include <dst/dst.h>

int main() {
    DstParseResult pres;
    DstAssembleOptions opts;
    DstAssembleResult ares;
    DstFunction *func;

    FILE *f = fopen("./dsts/minimal.dsts", "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  //same as rewind(f);

    char *string = malloc(fsize + 1);
    fread(string, fsize, 1, f);
    fclose(f);

    string[fsize] = 0;

    dst_init();

    pres = dst_parsec(string);
    free(string);

    if (pres.status == DST_PARSE_ERROR) {
        dst_puts(dst_formatc("parse error at %d: %s\n", pres.bytes_read, pres.result.error));
        return 1;
    }
    assert(pres.status == DST_PARSE_OK);
    dst_puts(dst_formatc("\nparse result: %v\n\n", pres.result.value));

    opts.flags = 0;
    opts.source = pres.result.value;
    opts.parsemap = dst_wrap_nil();

    ares = dst_asm(opts);
    if (ares.status == DST_ASSEMBLE_ERROR) {
        dst_puts(dst_formatc("assembly error: %s\n", ares.result.error));
        return 1;
    }
    assert(ares.status == DST_ASSEMBLE_OK);

    func = dst_asm_func(ares);
    
    dst_run(dst_wrap_function(func));
    dst_puts(dst_formatc("result: %v\n", dst_vm_fiber->ret));

    return 0;
}
