#ifndef ZACLR_TRACE_CONFIG_H
#define ZACLR_TRACE_CONFIG_H

#include <kernel/zaclr/include/zaclr_types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_trace_category {
    ZACLR_TRACE_CATEGORY_BOOT = 0,
    ZACLR_TRACE_CATEGORY_HOST = 1,
    ZACLR_TRACE_CATEGORY_RUNTIME = 2,
    ZACLR_TRACE_CATEGORY_PROCESS = 3,
    ZACLR_TRACE_CATEGORY_LOADER = 4,
    ZACLR_TRACE_CATEGORY_METADATA = 5,
    ZACLR_TRACE_CATEGORY_EXEC = 6,
    ZACLR_TRACE_CATEGORY_HEAP = 7,
    ZACLR_TRACE_CATEGORY_INTEROP = 8,
    ZACLR_TRACE_CATEGORY_DIAG = 9,
    ZACLR_TRACE_CATEGORY_TEST = 10
};

struct zaclr_trace_config {
    uint32_t enabled_mask;
    uint32_t minimum_event_id;
    bool enable_text;
    bool enable_hex;
};

#define ZACLR_TRACE_MASK_BOOT      (1u << ZACLR_TRACE_CATEGORY_BOOT)
#define ZACLR_TRACE_MASK_HOST      (1u << ZACLR_TRACE_CATEGORY_HOST)
#define ZACLR_TRACE_MASK_RUNTIME   (1u << ZACLR_TRACE_CATEGORY_RUNTIME)
#define ZACLR_TRACE_MASK_PROCESS   (1u << ZACLR_TRACE_CATEGORY_PROCESS)
#define ZACLR_TRACE_MASK_LOADER    (1u << ZACLR_TRACE_CATEGORY_LOADER)
#define ZACLR_TRACE_MASK_METADATA  (1u << ZACLR_TRACE_CATEGORY_METADATA)
#define ZACLR_TRACE_MASK_EXEC      (1u << ZACLR_TRACE_CATEGORY_EXEC)
#define ZACLR_TRACE_MASK_HEAP      (1u << ZACLR_TRACE_CATEGORY_HEAP)
#define ZACLR_TRACE_MASK_INTEROP   (1u << ZACLR_TRACE_CATEGORY_INTEROP)
#define ZACLR_TRACE_MASK_DIAG      (1u << ZACLR_TRACE_CATEGORY_DIAG)
#define ZACLR_TRACE_MASK_TEST      (1u << ZACLR_TRACE_CATEGORY_TEST)
#define ZACLR_TRACE_MASK_ALL       0xFFFFFFFFu

static inline struct zaclr_trace_config zaclr_trace_config_default(void)
{
    struct zaclr_trace_config config;
    config.enabled_mask = ZACLR_TRACE_MASK_ALL;
    config.minimum_event_id = 0u;
    config.enable_text = true;
    config.enable_hex = true;
    return config;
}

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TRACE_CONFIG_H */
