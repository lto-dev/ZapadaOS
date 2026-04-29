/*
 * Zapada - src/kernel/irq/irq_router.c
 *
 * Generic IRQ-to-channel router. The fixed subscription table keeps interrupt
 * delivery allocation-free.
 */

#include <kernel/irq/irq_router.h>
#include <kernel/support/kernel_memory.h>

typedef struct {
    int32_t      handle;
    uint32_t     irq_number;
    ipc_handle_t channel;
    uint64_t     sequence;
} irq_subscription_t;

#define IRQ_ROUTER_MAX_SUBSCRIPTIONS 16u

static irq_subscription_t s_subscriptions[IRQ_ROUTER_MAX_SUBSCRIPTIONS];
static uint32_t s_next_generation = 1u;

uint64_t irq_router_enter_critical(void)
{
#if defined(__x86_64__)
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
#elif defined(__aarch64__)
    uint64_t daif;
    __asm__ volatile ("mrs %0, daif; msr daifset, #2; isb" : "=r"(daif) : : "memory");
    return daif;
#else
    return 0u;
#endif
}

void irq_router_leave_critical(uint64_t state)
{
#if defined(__x86_64__)
    if ((state & (1ull << 9u)) != 0u) {
        __asm__ volatile ("sti" : : : "memory");
    } else {
        __asm__ volatile ("cli" : : : "memory");
    }
#elif defined(__aarch64__)
    if ((state & (1ull << 7u)) == 0u) {
        __asm__ volatile ("msr daifclr, #2; isb" : : : "memory");
    } else {
        __asm__ volatile ("msr daifset, #2; isb" : : : "memory");
    }
#else
    (void)state;
#endif
}

static int32_t make_subscription_handle(uint32_t index)
{
    uint32_t generation = s_next_generation++;
    if (s_next_generation == 0u) {
        s_next_generation = 1u;
    }

    return (int32_t)(((generation & 0x00FFFFFFu) << 8u) | (index + 1u));
}

int32_t irq_router_subscribe(uint32_t irq_number, ipc_handle_t channel)
{
    uint64_t state;
    int32_t handle;

    if (channel == IPC_HANDLE_INVALID || !ipc_channel_is_open(channel)) {
        return IRQ_ROUTER_STATUS_INVALID;
    }

    state = irq_router_enter_critical();

    for (uint32_t i = 0u; i < IRQ_ROUTER_MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].handle == 0) {
            handle = make_subscription_handle(i);
            s_subscriptions[i].handle = handle;
            s_subscriptions[i].irq_number = irq_number;
            s_subscriptions[i].channel = channel;
            s_subscriptions[i].sequence = 0u;
            irq_router_leave_critical(state);
            return handle;
        }
    }

    irq_router_leave_critical(state);
    return IRQ_ROUTER_STATUS_INVALID;
}

int32_t irq_router_unsubscribe(int32_t subscription_handle)
{
    uint64_t state;

    if (subscription_handle <= 0) {
        return IRQ_ROUTER_STATUS_INVALID;
    }

    state = irq_router_enter_critical();

    for (uint32_t i = 0u; i < IRQ_ROUTER_MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].handle == subscription_handle) {
            s_subscriptions[i].handle = 0;
            s_subscriptions[i].irq_number = 0u;
            s_subscriptions[i].channel = IPC_HANDLE_INVALID;
            s_subscriptions[i].sequence = 0u;
            irq_router_leave_critical(state);
            return IRQ_ROUTER_STATUS_OK;
        }
    }

    irq_router_leave_critical(state);
    return IRQ_ROUTER_STATUS_INVALID;
}

void irq_router_publish(uint32_t irq_number)
{
    ipc_message_t message;

    for (uint32_t i = 0u; i < IRQ_ROUTER_MAX_SUBSCRIPTIONS; i++) {
        irq_subscription_t *subscription = &s_subscriptions[i];
        if (subscription->handle <= 0 || subscription->irq_number != irq_number) {
            continue;
        }

        kernel_memset(&message, 0, sizeof(message));
        message.type = IRQ_ROUTER_MESSAGE_TYPE;
        message.payload_len = IRQ_ROUTER_MESSAGE_PAYLOAD_BYTES;
        message.payload[0] = (uint64_t)irq_number;
        message.payload[1] = ++subscription->sequence;

        (void)ipc_trysend(subscription->channel, &message);
    }
}

uint32_t irq_router_subscription_count(uint32_t irq_number)
{
    uint64_t state;
    uint32_t count = 0u;

    state = irq_router_enter_critical();

    for (uint32_t i = 0u; i < IRQ_ROUTER_MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].handle > 0 && s_subscriptions[i].irq_number == irq_number) {
            count++;
        }
    }

    irq_router_leave_critical(state);
    return count;
}
