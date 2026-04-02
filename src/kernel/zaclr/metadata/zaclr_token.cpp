#include <kernel/zaclr/metadata/zaclr_token.h>

extern "C" struct zaclr_token zaclr_token_make(uint32_t raw)
{
    struct zaclr_token token;
    token.raw = raw;
    return token;
}

extern "C" uint32_t zaclr_token_table(const struct zaclr_token* token)
{
    return token != NULL ? ((token->raw >> 24) & 0xFFu) : (uint32_t)ZACLR_TOKEN_TABLE_INVALID;
}

extern "C" uint32_t zaclr_token_row(const struct zaclr_token* token)
{
    return token != NULL ? (token->raw & 0x00FFFFFFu) : 0u;
}

extern "C" bool zaclr_token_is_nil(const struct zaclr_token* token)
{
    return token == NULL || zaclr_token_row(token) == 0u;
}

extern "C" bool zaclr_token_matches_table(const struct zaclr_token* token, enum zaclr_token_table table)
{
    return token != NULL && zaclr_token_table(token) == (uint32_t)table && !zaclr_token_is_nil(token);
}
