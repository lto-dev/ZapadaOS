#ifndef ZACLR_STRING_H
#define ZACLR_STRING_H

#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_string_desc {
    struct zaclr_object_desc object;
    uint32_t length;
    uint32_t byte_length;
};

struct zaclr_result zaclr_string_allocate_ascii(struct zaclr_heap* heap,
                                                const char* text,
                                                uint32_t length,
                                                zaclr_object_handle* out_handle);
struct zaclr_result zaclr_string_allocate_utf16(struct zaclr_heap* heap,
                                                const uint16_t* text,
                                                uint32_t length,
                                                zaclr_object_handle* out_handle);
struct zaclr_string_desc* zaclr_string_from_handle(struct zaclr_heap* heap,
                                                   zaclr_object_handle handle);
const struct zaclr_string_desc* zaclr_string_from_handle_const(const struct zaclr_heap* heap,
                                                               zaclr_object_handle handle);
const char* zaclr_string_chars(const struct zaclr_string_desc* value);
const char* zaclr_string_chars_from_handle(const struct zaclr_heap* heap,
                                           zaclr_object_handle handle);
const uint16_t* zaclr_string_code_units(const struct zaclr_string_desc* value);
uint16_t zaclr_string_char_at(const struct zaclr_string_desc* value,
                              uint32_t index);
uint32_t zaclr_string_length(const struct zaclr_string_desc* value);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_STRING_H */
