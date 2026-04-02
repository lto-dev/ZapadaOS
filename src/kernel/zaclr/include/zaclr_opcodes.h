#ifndef ZACLR_OPCODES_H
#define ZACLR_OPCODES_H

#include <kernel/zaclr/include/zaclr_types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_opcode {
#define OPDEF(c, s, pop, push, args, type, l, s1, s2, ctrl) c = ((l) == 1 ? (uint16_t)(s2) : (uint16_t)(((uint16_t)(s1) << 8) | (uint16_t)(s2))),
#include "../../../../build/generated/zaclr/opcode_table.inc"
#undef OPDEF
    CEE_COUNT,
};

struct zaclr_opcode_desc {
    enum zaclr_opcode opcode;
    const char* name;
    enum zaclr_operand_kind operand_kind;
};

const struct zaclr_opcode_desc* zaclr_opcode_table_get(void);
size_t zaclr_opcode_table_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_OPCODES_H */
