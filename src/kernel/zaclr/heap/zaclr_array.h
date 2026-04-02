#ifndef ZACLR_ARRAY_H
#define ZACLR_ARRAY_H

#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/metadata/zaclr_token.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_array_desc {
    struct zaclr_object_desc object;
    struct zaclr_token element_type;
    uint32_t element_size;
    uint32_t length;
    uint32_t data_size;
};

struct zaclr_result zaclr_array_allocate(struct zaclr_heap* heap,
                                         zaclr_type_id type_id,
                                         struct zaclr_token element_type,
                                         uint32_t element_size,
                                         uint32_t length,
                                         zaclr_object_handle* out_handle);
struct zaclr_array_desc* zaclr_array_from_handle(struct zaclr_heap* heap,
                                                 zaclr_object_handle handle);
const struct zaclr_array_desc* zaclr_array_from_handle_const(const struct zaclr_heap* heap,
                                                             zaclr_object_handle handle);
uint32_t zaclr_array_length(const struct zaclr_array_desc* value);
void* zaclr_array_data(struct zaclr_array_desc* value);
const void* zaclr_array_data_const(const struct zaclr_array_desc* value);
uint32_t zaclr_array_element_size(const struct zaclr_array_desc* value);
struct zaclr_token zaclr_array_element_type(const struct zaclr_array_desc* value);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ARRAY_H */
