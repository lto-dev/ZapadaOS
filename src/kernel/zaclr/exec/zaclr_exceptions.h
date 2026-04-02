#ifndef ZACLR_EXCEPTIONS_H
#define ZACLR_EXCEPTIONS_H

#include <kernel/zaclr/heap/zaclr_object.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_exception_state {
    zaclr_object_handle current_exception;
    uint32_t flags;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_EXCEPTIONS_H */
