/*
 * Zapada - src/kernel/irq/irq_router.h
 *
 * Generic IRQ-to-channel router for the managed driver HAL.
 */

#ifndef ZAPADA_IRQ_ROUTER_H
#define ZAPADA_IRQ_ROUTER_H

#include <kernel/ipc/ipc.h>
#include <kernel/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRQ_ROUTER_STATUS_OK          0
#define IRQ_ROUTER_STATUS_INVALID    -1
#define IRQ_ROUTER_STATUS_UNSUPPORTED -2

#define IRQ_ROUTER_TIMER_IRQ 0u
#define IRQ_ROUTER_MESSAGE_TYPE (IPC_MSG_TYPE_USER + 0x100u)
#define IRQ_ROUTER_MESSAGE_PAYLOAD_BYTES 16u

uint64_t irq_router_enter_critical(void);
void irq_router_leave_critical(uint64_t state);

int32_t irq_router_subscribe(uint32_t irq_number, ipc_handle_t channel);
int32_t irq_router_unsubscribe(int32_t subscription_handle);
void irq_router_publish(uint32_t irq_number);
uint32_t irq_router_subscription_count(uint32_t irq_number);

#ifdef __cplusplus
}
#endif

#endif /* ZAPADA_IRQ_ROUTER_H */
