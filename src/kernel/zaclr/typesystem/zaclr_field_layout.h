#ifndef ZACLR_FIELD_LAYOUT_H
#define ZACLR_FIELD_LAYOUT_H

#include <kernel/zaclr/metadata/zaclr_signature.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the size in bytes for a given ELEMENT_TYPE constant.
   Returns 0 for unsupported or unknown element types. */
uint32_t zaclr_field_layout_size_from_element_type(uint8_t element_type);

/* Returns 1 if the given element type represents a managed object reference
   (CLASS, OBJECT, STRING, SZARRAY, ARRAY), 0 otherwise. */
uint8_t zaclr_field_layout_is_reference(uint8_t element_type);

/* Returns the natural alignment requirement in bytes for a given element type.
   The alignment is typically the same as the field size, capped at 8 bytes. */
uint32_t zaclr_field_layout_compute_alignment(uint8_t element_type);

/* Parses a field signature blob and extracts the element type into out_type.
   The field signature format is: FIELD (0x06) followed by the type encoding.
   Returns ZACLR_STATUS_OK on success. */
struct zaclr_result zaclr_field_layout_parse_field_signature(const struct zaclr_slice* blob,
                                                             struct zaclr_signature_type* out_type);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_FIELD_LAYOUT_H */
