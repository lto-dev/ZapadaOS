#ifndef ZACLR_STATUS_H
#define ZACLR_STATUS_H

#include <kernel/zaclr/include/zaclr_types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_status {
    ZACLR_STATUS_OK = 0,
    ZACLR_STATUS_NOT_IMPLEMENTED = 1,
    ZACLR_STATUS_INVALID_ARGUMENT = 2,
    ZACLR_STATUS_OUT_OF_MEMORY = 3,
    ZACLR_STATUS_NOT_FOUND = 4,
    ZACLR_STATUS_ALREADY_EXISTS = 5,
    ZACLR_STATUS_BUFFER_TOO_SMALL = 6,
    ZACLR_STATUS_UNSUPPORTED = 7,
    ZACLR_STATUS_BAD_STATE = 8,
    ZACLR_STATUS_BAD_IMAGE = 9,
    ZACLR_STATUS_BAD_METADATA = 10,
    ZACLR_STATUS_DISPATCH_ERROR = 11,
    ZACLR_STATUS_TRACE_DROPPED = 12
};

enum zaclr_status_category {
    ZACLR_STATUS_CATEGORY_GENERAL = 0,
    ZACLR_STATUS_CATEGORY_HOST = 1,
    ZACLR_STATUS_CATEGORY_RUNTIME = 2,
    ZACLR_STATUS_CATEGORY_PROCESS = 3,
    ZACLR_STATUS_CATEGORY_LOADER = 4,
    ZACLR_STATUS_CATEGORY_METADATA = 5,
    ZACLR_STATUS_CATEGORY_EXEC = 6,
    ZACLR_STATUS_CATEGORY_HEAP = 7,
    ZACLR_STATUS_CATEGORY_INTEROP = 8,
    ZACLR_STATUS_CATEGORY_DIAG = 9
};

struct zaclr_result {
    enum zaclr_status status;
    enum zaclr_status_category category;
};

static inline struct zaclr_result zaclr_result_ok(void)
{
    struct zaclr_result result;
    result.status = ZACLR_STATUS_OK;
    result.category = ZACLR_STATUS_CATEGORY_GENERAL;
    return result;
}

static inline struct zaclr_result zaclr_result_make(enum zaclr_status status, enum zaclr_status_category category)
{
    struct zaclr_result result;
    result.status = status;
    result.category = category;
    return result;
}

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_STATUS_H */
