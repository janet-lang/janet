#ifndef TESTS_H_DNMBUYYL
#define TESTS_H_DNMBUYYL

#include <stdint.h>
#include <inttypes.h>

/* Copy of util.h - compiler stats */
extern int64_t total_instruction_count;
extern int64_t total_optimize_fixpoint_loops;
extern int64_t total_funcdefs_optimized;

/* Tests */
extern int array_test();
extern int buffer_test();
extern int number_test();
extern int system_test();
extern int table_test();

#endif /* end of include guard: TESTS_H_DNMBUYYL */
