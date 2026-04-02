#ifndef ZACLR_TYPES_H
#define ZACLR_TYPES_H

#include <kernel/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t zaclr_process_id;
typedef uint32_t zaclr_thread_id;
typedef uint32_t zaclr_frame_id;
typedef uint32_t zaclr_assembly_id;
typedef uint32_t zaclr_method_id;
typedef uint32_t zaclr_type_id;
typedef uint32_t zaclr_object_handle;
typedef uint32_t zaclr_app_domain_id;
typedef uint32_t zaclr_handle_table_id;
typedef uint32_t zaclr_security_context_id;
typedef uint32_t zaclr_address_space_id;
typedef uint32_t zaclr_static_store_id;
typedef uint32_t zaclr_thread_group_id;
typedef uint32_t zaclr_assembly_set_id;
typedef uint32_t zaclr_type_static_map_id;
typedef uint32_t zaclr_user_id;
typedef uint32_t zaclr_group_id;
typedef uint32_t zaclr_trace_event_id;
typedef uint32_t zaclr_native_call_id;
typedef uint32_t zaclr_handle_value;

enum zaclr_thread_state {
    ZACLR_THREAD_STATE_CREATED = 0,
    ZACLR_THREAD_STATE_READY = 1,
    ZACLR_THREAD_STATE_RUNNING = 2,
    ZACLR_THREAD_STATE_WAITING = 3,
    ZACLR_THREAD_STATE_SUSPENDED = 4,
    ZACLR_THREAD_STATE_STOPPED = 5
};

enum zaclr_operand_kind {
    InlineNone = 0,
    InlineVar = 1,
    InlineI = 2,
    InlineR = 3,
    InlineBrTarget = 4,
    InlineI8 = 5,
    InlineMethod = 6,
    InlineField = 7,
    InlineType = 8,
    InlineString = 9,
    InlineSig = 10,
    InlineRVA = 11,
    InlineTok = 12,
    InlineSwitch = 13,
    InlinePhi = 14,
    ShortInline = 16,
    PrimaryMask = (ShortInline - 1),
    ShortInlineVar = (ShortInline + InlineVar),
    ShortInlineI = (ShortInline + InlineI),
    ShortInlineR = (ShortInline + InlineR),
    ShortInlineBrTarget = (ShortInline + InlineBrTarget),
    InlineOpcode = (ShortInline + InlineNone)
};

struct zaclr_slice {
    const uint8_t* data;
    size_t size;
};

struct zaclr_name_view {
    const char* text;
    size_t length;
};

struct zaclr_stdio_binding {
    zaclr_handle_value stdin_handle;
    zaclr_handle_value stdout_handle;
    zaclr_handle_value stderr_handle;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TYPES_H */
