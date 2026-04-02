#ifndef ZACLR_EXEC_THREAD_H
#define ZACLR_EXEC_THREAD_H

#include <kernel/zaclr/process/zaclr_process.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_exec_thread {
    struct zaclr_thread identity;
    uint32_t flags;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_EXEC_THREAD_H */
