#include <dst/dst.h>

int dst_init_(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_def(env, "pi", dst_wrap_real(M_PI));
    return 0;
}
