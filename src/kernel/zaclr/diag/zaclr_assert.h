#ifndef ZACLR_ASSERT_H
#define ZACLR_ASSERT_H

#include <kernel/zaclr/include/zaclr_trace.h>

#ifdef __cplusplus
extern "C" {
#endif

void zaclr_assert_fail(const struct zaclr_runtime* runtime,
                       const char* file,
                       int line,
                       const char* expression);

#define ZACLR_ASSERT(runtime, expression) \
    do { \
        if (!(expression)) { \
            zaclr_assert_fail((runtime), __FILE__, __LINE__, #expression); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ASSERT_H */
