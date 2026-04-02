#ifndef ZACLR_SMOKE_TESTS_H
#define ZACLR_SMOKE_TESTS_H

#include <kernel/zaclr/runtime/zaclr_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_result zaclr_run_smoke_tests(struct zaclr_runtime* runtime);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_SMOKE_TESTS_H */
