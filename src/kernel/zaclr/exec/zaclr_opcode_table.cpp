#include <kernel/zaclr/include/zaclr_opcodes.h>

namespace {

const struct zaclr_opcode_desc g_zaclr_opcode_table[] = {
#include "../../../../build/generated/zaclr/opcode_desc_table.inc"
};

} /* namespace */

extern "C" const struct zaclr_opcode_desc* zaclr_opcode_table_get(void)
{
    return g_zaclr_opcode_table;
}

extern "C" size_t zaclr_opcode_table_count(void)
{
    return sizeof(g_zaclr_opcode_table) / sizeof(g_zaclr_opcode_table[0]);
}
