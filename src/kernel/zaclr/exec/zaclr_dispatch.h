#ifndef ZACLR_DISPATCH_H
#define ZACLR_DISPATCH_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/include/zaclr_opcodes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_dispatch_context {
    struct zaclr_runtime* runtime;
    struct zaclr_engine* engine;
    struct zaclr_frame** current_frame;
};

struct zaclr_result zaclr_dispatch_step(struct zaclr_dispatch_context* context,
                                        enum zaclr_opcode opcode);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_DISPATCH_H */
