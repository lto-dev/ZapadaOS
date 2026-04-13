#ifndef ZACLR_PINVOKE_RESOLVER_H
#define ZACLR_PINVOKE_RESOLVER_H

#include <kernel/zaclr/metadata/zaclr_method_map.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_method_dispatch_kind {
    ZACLR_DISPATCH_KIND_IL_BODY = 0,
    ZACLR_DISPATCH_KIND_INTERNAL_CALL = 1,
    ZACLR_DISPATCH_KIND_QCALL = 2,
    ZACLR_DISPATCH_KIND_PINVOKE = 3,
    ZACLR_DISPATCH_KIND_INTRINSIC = 4,
    ZACLR_DISPATCH_KIND_NOT_IMPLEMENTED = 5
};

struct zaclr_method_dispatch_info {
    enum zaclr_method_dispatch_kind kind;
    const char* qcall_entry_point;
    const char* pinvoke_module;
    const char* pinvoke_import;
};

struct zaclr_result zaclr_classify_method_dispatch(const struct zaclr_method_desc* method,
                                                   struct zaclr_method_dispatch_info* out_info);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PINVOKE_RESOLVER_H */
