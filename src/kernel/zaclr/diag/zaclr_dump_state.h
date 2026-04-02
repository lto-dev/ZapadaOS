#ifndef ZACLR_DUMP_STATE_H
#define ZACLR_DUMP_STATE_H

#include <kernel/zaclr/include/zaclr_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

void zaclr_dump_runtime_state(const struct zaclr_runtime* runtime);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_DUMP_STATE_H */
