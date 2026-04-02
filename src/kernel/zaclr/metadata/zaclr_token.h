#ifndef ZACLR_TOKEN_H
#define ZACLR_TOKEN_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_token_table {
    ZACLR_TOKEN_TABLE_INVALID = 0xFF,
    ZACLR_TOKEN_TABLE_MODULE = 0x00,
    ZACLR_TOKEN_TABLE_TYPEREF = 0x01,
    ZACLR_TOKEN_TABLE_TYPEDEF = 0x02,
    ZACLR_TOKEN_TABLE_FIELD = 0x04,
    ZACLR_TOKEN_TABLE_METHOD = 0x06,
    ZACLR_TOKEN_TABLE_MEMBERREF = 0x0A,
    ZACLR_TOKEN_TABLE_TYPESPEC = 0x1B,
    ZACLR_TOKEN_TABLE_ASSEMBLYREF = 0x23,
    ZACLR_TOKEN_TABLE_METHODSPEC = 0x2B
};

struct zaclr_token {
    uint32_t raw;
};

struct zaclr_token zaclr_token_make(uint32_t raw);
uint32_t zaclr_token_table(const struct zaclr_token* token);
uint32_t zaclr_token_row(const struct zaclr_token* token);
bool zaclr_token_is_nil(const struct zaclr_token* token);
bool zaclr_token_matches_table(const struct zaclr_token* token, enum zaclr_token_table table);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TOKEN_H */
