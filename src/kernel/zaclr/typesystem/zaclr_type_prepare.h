#ifndef ZACLR_TYPE_PREPARE_H
#define ZACLR_TYPE_PREPARE_H

#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;
struct zaclr_loaded_assembly;
struct zaclr_type_desc;

/* Object header size assumed for instance_size calculation.
   This corresponds to the size of zaclr_object_desc (handle, type_id, ptr, size, flags, family, gc). */
#define ZACLR_OBJECT_HEADER_SIZE ((uint32_t)sizeof(struct zaclr_object_desc))

/* Prepares a MethodTable for a given type descriptor.
   - Resolves parent type and prepares it recursively
   - Computes flags from TypeDef attributes and parent chain
   - Computes field layout (byte offsets, sizes, reference tracking)
   - Builds vtable (copies parent slots, overrides, new virtuals)
   - Caches the result in the assembly's method_table_cache

   If the type has already been prepared, returns the cached MethodTable.

   Parameters:
     runtime           - the runtime context (for cross-assembly resolution)
     assembly          - the assembly containing this type
     type_desc         - the metadata-level type descriptor to prepare
     out_method_table  - receives the pointer to the prepared MethodTable

   Returns ZACLR_STATUS_OK on success. */
struct zaclr_result zaclr_type_prepare(struct zaclr_runtime* runtime,
                                       struct zaclr_loaded_assembly* assembly,
                                       const struct zaclr_type_desc* type_desc,
                                       struct zaclr_method_table** out_method_table);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TYPE_PREPARE_H */
