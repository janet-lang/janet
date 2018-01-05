#include "unit.h"
#include <dst/dst.h>

int main() {
    DstParseResult pres;
    DstAssembleOptions opts;
    DstAssembleResult ares;
    DstFunction *func;

    FILE *f = fopen("./dsttest/minimal.dsts", "rb");
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
        dst_puts(dst_formatc("parse error at %d: %S\n", pres.bytes_read, pres.error));
        return 1;
    }
    assert(pres.status == DST_PARSE_OK);
    dst_puts(dst_formatc("\nparse result: %v\n\n", pres.value));

    opts.flags = 0;
    opts.source = pres.value;
    opts.sourcemap = pres.map;

    ares = dst_asm(opts);
    if (ares.status == DST_ASSEMBLE_ERROR) {
        dst_puts(dst_formatc("assembly error: %S\n", ares.error));
        dst_puts(dst_formatc("error location: %d, %d\n", ares.error_start, ares.error_end));
        return 1;
    }
    assert(ares.status == DST_ASSEMBLE_OK);

    func = dst_asm_func(ares);

    dst_puts(dst_formatc("\nfuncdef: %v\n\n", dst_disasm(ares.funcdef)));
    
    dst_run(dst_wrap_function(func));
    dst_puts(dst_formatc("result: %v\n", dst_vm_fiber->ret));

    dst_deinit();

    return 0;
}
