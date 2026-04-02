#ifndef ZACLR_TYPE_SYSTEM_H
#define ZACLR_TYPE_SYSTEM_H

#include <kernel/zaclr/runtime/zaclr_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_member_name_ref {
    const char* type_namespace;
    const char* type_name;
    const char* method_name;
};

struct zaclr_memberref_target {
    struct zaclr_member_name_ref key;
    const char* assembly_name;
    struct zaclr_signature_desc signature;
    uint32_t class_token;
};

bool zaclr_text_equals(const char* left, const char* right);
struct zaclr_result zaclr_metadata_get_typeref_name(const struct zaclr_metadata_reader* reader,
                                                    uint32_t row_1based,
                                                    struct zaclr_member_name_ref* out_name);
struct zaclr_result zaclr_metadata_get_assemblyref_name(const struct zaclr_metadata_reader* reader,
                                                        uint32_t row_1based,
                                                        const char** out_name);
struct zaclr_result zaclr_metadata_get_typeref_assembly_name(const struct zaclr_metadata_reader* reader,
                                                             uint32_t row_1based,
                                                             const char** out_assembly_name);
struct zaclr_result zaclr_metadata_get_type_name(const struct zaclr_loaded_assembly* assembly,
                                                 struct zaclr_token token,
                                                 struct zaclr_member_name_ref* out_name);
const struct zaclr_type_desc* zaclr_type_system_find_type_by_name(const struct zaclr_loaded_assembly* assembly,
                                                                  const struct zaclr_member_name_ref* name);
struct zaclr_result zaclr_type_system_resolve_exported_type_forwarder(const struct zaclr_loaded_assembly* assembly,
                                                                      struct zaclr_runtime* runtime,
                                                                      const struct zaclr_member_name_ref* name,
                                                                      const struct zaclr_loaded_assembly** out_assembly,
                                                                      const struct zaclr_type_desc** out_type);
struct zaclr_result zaclr_type_system_resolve_type_desc(const struct zaclr_loaded_assembly* current_assembly,
                                                        struct zaclr_runtime* runtime,
                                                        struct zaclr_token token,
                                                        const struct zaclr_loaded_assembly** out_assembly,
                                                        const struct zaclr_type_desc** out_type);
struct zaclr_result zaclr_type_system_resolve_external_named_type(struct zaclr_runtime* runtime,
                                                                  const char* preferred_assembly_name,
                                                                  const struct zaclr_member_name_ref* name,
                                                                  const struct zaclr_loaded_assembly** out_assembly,
                                                                  const struct zaclr_type_desc** out_type);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TYPE_SYSTEM_H */
