#ifndef ZACLR_METHOD_HANDLE_H
#define ZACLR_METHOD_HANDLE_H

#include <kernel/zaclr/include/zaclr_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_method_handle {
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_method_desc* method;
    struct zaclr_method_locator locator;
};

struct zaclr_result zaclr_method_handle_create(const struct zaclr_loaded_assembly* assembly,
                                               const struct zaclr_method_desc* method,
                                               struct zaclr_method_handle* out_handle);
uintptr_t zaclr_method_handle_pack(const struct zaclr_method_handle* handle);
struct zaclr_result zaclr_method_handle_unpack(uintptr_t packed,
                                               struct zaclr_method_handle* out_handle);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_METHOD_HANDLE_H */
