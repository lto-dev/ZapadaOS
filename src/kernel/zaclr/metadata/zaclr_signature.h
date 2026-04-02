#ifndef ZACLR_SIGNATURE_H
#define ZACLR_SIGNATURE_H

#include <kernel/zaclr/metadata/zaclr_token.h>

#define ZACLR_SIGNATURE_FLAG_METHOD 0x00000001u

enum zaclr_element_type {
    ZACLR_ELEMENT_TYPE_END = 0x00,
    ZACLR_ELEMENT_TYPE_VOID = 0x01,
    ZACLR_ELEMENT_TYPE_BOOLEAN = 0x02,
    ZACLR_ELEMENT_TYPE_CHAR = 0x03,
    ZACLR_ELEMENT_TYPE_I1 = 0x04,
    ZACLR_ELEMENT_TYPE_U1 = 0x05,
    ZACLR_ELEMENT_TYPE_I2 = 0x06,
    ZACLR_ELEMENT_TYPE_U2 = 0x07,
    ZACLR_ELEMENT_TYPE_I4 = 0x08,
    ZACLR_ELEMENT_TYPE_U4 = 0x09,
    ZACLR_ELEMENT_TYPE_I8 = 0x0A,
    ZACLR_ELEMENT_TYPE_U8 = 0x0B,
    ZACLR_ELEMENT_TYPE_R4 = 0x0C,
    ZACLR_ELEMENT_TYPE_R8 = 0x0D,
    ZACLR_ELEMENT_TYPE_STRING = 0x0E,
    ZACLR_ELEMENT_TYPE_PTR = 0x0F,
    ZACLR_ELEMENT_TYPE_BYREF = 0x10,
    ZACLR_ELEMENT_TYPE_VALUETYPE = 0x11,
    ZACLR_ELEMENT_TYPE_CLASS = 0x12,
    ZACLR_ELEMENT_TYPE_VAR = 0x13,
    ZACLR_ELEMENT_TYPE_ARRAY = 0x14,
    ZACLR_ELEMENT_TYPE_GENERICINST = 0x15,
    ZACLR_ELEMENT_TYPE_TYPEDBYREF = 0x16,
    ZACLR_ELEMENT_TYPE_I = 0x18,
    ZACLR_ELEMENT_TYPE_U = 0x19,
    ZACLR_ELEMENT_TYPE_FNPTR = 0x1B,
    ZACLR_ELEMENT_TYPE_OBJECT = 0x1C,
    ZACLR_ELEMENT_TYPE_SZARRAY = 0x1D,
    ZACLR_ELEMENT_TYPE_MVAR = 0x1E,
    ZACLR_ELEMENT_TYPE_CMOD_REQD = 0x1F,
    ZACLR_ELEMENT_TYPE_CMOD_OPT = 0x20,
    ZACLR_ELEMENT_TYPE_SENTINEL = 0x41,
    ZACLR_ELEMENT_TYPE_PINNED = 0x45
};

enum zaclr_signature_type_flags {
    ZACLR_SIGNATURE_TYPE_FLAG_NONE = 0x00000000u,
    ZACLR_SIGNATURE_TYPE_FLAG_BYREF = 0x00000001u,
    ZACLR_SIGNATURE_TYPE_FLAG_PINNED = 0x00000002u
};

struct zaclr_signature_type {
    uint8_t element_type;
    uint8_t reserved0;
    uint16_t reserved1;
    struct zaclr_token type_token;
    uint32_t generic_param_index;
    uint32_t flags;
};

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_signature_desc {
    struct zaclr_slice blob;
    uint8_t calling_convention;
    uint8_t header_size;
    uint16_t parameter_count;
    uint16_t generic_parameter_count;
    uint32_t parameter_types_offset;
    struct zaclr_signature_type return_type;
    uint32_t flags;
};

struct zaclr_generic_instantiation_desc {
    struct zaclr_slice blob;
    uint8_t calling_convention;
    uint8_t header_size;
    uint16_t argument_count;
};

struct zaclr_result zaclr_signature_parse_method(const struct zaclr_slice* blob,
                                                struct zaclr_signature_desc* signature);
struct zaclr_result zaclr_signature_parse_generic_instantiation(const struct zaclr_slice* blob,
                                                               struct zaclr_generic_instantiation_desc* instantiation);
struct zaclr_result zaclr_signature_read_method_parameter(const struct zaclr_signature_desc* signature,
                                                          uint32_t parameter_index,
                                                          struct zaclr_signature_type* parameter_type);
uint32_t zaclr_signature_flags(const struct zaclr_signature_desc* signature);
uint64_t zaclr_signature_hash_text(const char* text);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_SIGNATURE_H */
