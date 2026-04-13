#include <kernel/zaclr/heap/zaclr_heap.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_object.h>

namespace
{
    static const uint32_t ZACLR_HEAP_DEFAULT_THRESHOLD_BYTES = 64u * 1024u;

    static void zaclr_heap_release_node(struct zaclr_heap* heap, uint32_t index)
    {
        struct zaclr_heap_object_node* node;

        if (heap == NULL || index >= heap->node_count)
        {
            return;
        }

        node = &heap->nodes[index];
        if (node->used == 0u || node->object == NULL)
        {
            *node = {};
            return;
        }

        if (heap->allocated_bytes >= node->allocation_size)
        {
            heap->allocated_bytes -= node->allocation_size;
        }
        else
        {
            heap->allocated_bytes = 0u;
        }

        if (heap->live_object_count != 0u)
        {
            heap->live_object_count--;
        }

        kernel_free(node->object);
        *node = {};
    }

    static struct zaclr_result zaclr_heap_reserve_nodes(struct zaclr_heap* heap, uint32_t minimum_capacity)
    {
        struct zaclr_heap_object_node* nodes;
        uint32_t new_capacity;
        uint32_t index;

        if (heap == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        if (minimum_capacity <= heap->node_capacity)
        {
            return zaclr_result_ok();
        }

        new_capacity = heap->node_capacity == 0u ? 16u : heap->node_capacity;
        while (new_capacity < minimum_capacity)
        {
            if (new_capacity > (0xFFFFFFFFu / 2u))
            {
                return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_HEAP);
            }

            new_capacity *= 2u;
        }

        nodes = (struct zaclr_heap_object_node*)kernel_alloc(sizeof(struct zaclr_heap_object_node) * new_capacity);
        if (nodes == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_HEAP);
        }

        kernel_memset(nodes, 0, sizeof(struct zaclr_heap_object_node) * new_capacity);
        for (index = 0u; index < heap->node_count; ++index)
        {
            nodes[index] = heap->nodes[index];
        }

        if (heap->nodes != NULL)
        {
            kernel_free(heap->nodes);
        }

        heap->nodes = nodes;
        heap->node_capacity = new_capacity;
        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_heap_initialize(struct zaclr_heap* heap,
                                                       struct zaclr_runtime* runtime)
{
    if (heap == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *heap = {};
    heap->runtime = runtime;
    heap->collection_threshold_bytes = ZACLR_HEAP_DEFAULT_THRESHOLD_BYTES;
    return zaclr_result_ok();
}

extern "C" void zaclr_heap_reset(struct zaclr_heap* heap)
{
    uint32_t index;

    if (heap == NULL)
    {
        return;
    }

    for (index = 0u; index < heap->node_count; ++index)
    {
        zaclr_heap_release_node(heap, index);
    }

    if (heap->nodes != NULL)
    {
        kernel_free(heap->nodes);
    }

    *heap = {};
}

extern "C" struct zaclr_result zaclr_heap_allocate_object(struct zaclr_heap* heap,
                                                              size_t allocation_size,
                                                              const struct zaclr_loaded_assembly* owning_assembly,
                                                              zaclr_type_id type_id,
                                                              uint32_t object_family,
                                                              uint32_t object_flags,
                                                              struct zaclr_object_desc** out_object)
{
    struct zaclr_object_desc* object;
    struct zaclr_result result;
    uint32_t node_index;

    if (heap == NULL || out_object == NULL || allocation_size < sizeof(struct zaclr_object_desc))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_object = NULL;
    if (heap->runtime != NULL
        && heap->collection_threshold_bytes != 0u
        && allocation_size <= 0xFFFFFFFFu
        && heap->allocated_bytes > (heap->collection_threshold_bytes - (uint32_t)allocation_size))
    {
        result = zaclr_gc_collect(heap->runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    result = zaclr_heap_reserve_nodes(heap, heap->node_count + 1u);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    object = (struct zaclr_object_desc*)kernel_alloc(allocation_size);
    if (object == NULL)
    {
        if (heap->runtime != NULL && (heap->flags & ZACLR_HEAP_FLAG_GC_ACTIVE) == 0u)
        {
            result = zaclr_gc_collect(heap->runtime);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            object = (struct zaclr_object_desc*)kernel_alloc(allocation_size);
        }

        if (object == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_HEAP);
        }
    }

    kernel_memset(object, 0, allocation_size);
    object->size_bytes = (uint32_t)allocation_size;
    object->family = (uint16_t)object_family;
    object->gc_mark = 0u;
    object->gc_state = ZACLR_OBJECT_GC_STATE_NONE;
    object->header.sync_block_index = 0u;
    object->header.flags = object_flags;
    object->header.method_table = NULL;
    object->owning_assembly = owning_assembly;
    object->type_id = type_id;
    node_index = heap->node_count;
    heap->nodes[node_index].object = object;
    heap->nodes[node_index].allocation_size = (uint32_t)allocation_size;
    heap->nodes[node_index].used = 1u;
    heap->node_count++;
    heap->live_object_count++;
    heap->allocated_bytes += (uint32_t)allocation_size;
    *out_object = object;
    return zaclr_result_ok();
}

extern "C" struct zaclr_object_desc* zaclr_heap_get_object(const struct zaclr_heap* heap,
                                                               zaclr_object_handle handle)
{
    (void)heap;
    if (handle == 0u)
    {
        return NULL;
    }

    return (struct zaclr_object_desc*)(uintptr_t)handle;
}

extern "C" zaclr_object_handle zaclr_heap_get_object_handle(const struct zaclr_heap*,
                                                               const struct zaclr_object_desc* object)
{
    return (zaclr_object_handle)(uintptr_t)object;
}

extern "C" uint32_t zaclr_heap_live_object_count(const struct zaclr_heap* heap)
{
    return heap != NULL ? heap->live_object_count : 0u;
}

extern "C" uint32_t zaclr_heap_allocated_bytes(const struct zaclr_heap* heap)
{
    return heap != NULL ? heap->allocated_bytes : 0u;
}

extern "C" void zaclr_heap_clear_marks(struct zaclr_heap* heap)
{
    uint32_t index;

    if (heap == NULL)
    {
        return;
    }

    for (index = 0u; index < heap->node_count; ++index)
    {
        if (heap->nodes[index].used != 0u && heap->nodes[index].object != NULL)
        {
            heap->nodes[index].object->gc_mark = 0u;
        }
    }
}

extern "C" void zaclr_heap_sweep_unmarked(struct zaclr_heap* heap)
{
    uint32_t index;

    if (heap == NULL)
    {
        return;
    }

    for (index = 0u; index < heap->node_count; ++index)
    {
        struct zaclr_object_desc* object = heap->nodes[index].object;
        if (heap->nodes[index].used == 0u || object == NULL)
        {
            continue;
        }

        if (object->gc_mark == 0u && (object->gc_state & ZACLR_OBJECT_GC_STATE_PINNED) == 0u)
        {
            zaclr_heap_release_node(heap, index);
        }
    }

    heap->collection_count++;
}
