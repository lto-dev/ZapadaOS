#include <kernel/zaclr/exec/zaclr_intrinsics.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/exec/zaclr_dispatch_helpers.h>
#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/heap/zaclr_gc_roots.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static bool is_memberinfo_get_module_intrinsic(const struct zaclr_type_desc* type,
                                                   const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && zaclr_text_equals(type->type_namespace.text, "System.Reflection")
            && zaclr_text_equals(type->type_name.text, "MemberInfo")
            && zaclr_text_equals(method->name.text, "get_Module")
            && method->signature.parameter_count == 0u
            && (method->signature.calling_convention & 0x20u) != 0u;
    }

    static struct zaclr_result invoke_memberinfo_get_module_intrinsic(struct zaclr_frame* frame)
    {
        struct zaclr_stack_value this_value = {};
        struct zaclr_stack_value module_value = {};

        if (frame == NULL || frame->argument_count == 0u || frame->arguments == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        this_value = frame->arguments[0];
        if (this_value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || this_value.data.object_reference == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        module_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        module_value.data.object_reference = this_value.data.object_reference;
        return zaclr_eval_stack_push(&frame->eval_stack, &module_value);
    }

    /* RuntimeHelpers intrinsics reference map for future ZACLR bring-up:
       - CoreCLR managed declaration + [Intrinsic] marker:
         CLONES/runtime/src/coreclr/System.Private.CoreLib/src/System/Runtime/CompilerServices/RuntimeHelpers.CoreCLR.cs
       - CoreCLR method identity table used by the runtime:
         CLONES/runtime/src/coreclr/vm/corelib.h
       - CoreCLR EE/JIT-side replacement logic:
         CLONES/runtime/src/coreclr/vm/jitinterface.cpp
       - NativeAOT / shared-compiler fallback stub generation:
         CLONES/runtime/src/coreclr/tools/Common/TypeSystem/IL/Stubs/RuntimeHelpersIntrinsics.cs
         CLONES/runtime/src/coreclr/nativeaot/System.Private.CoreLib/src/System/Runtime/CompilerServices/RuntimeHelpers.NativeAot.cs

       RuntimeHelpers methods currently listed in corelib.h that should be reviewed as mandatory runtime
       intrinsics / EE-owned helpers instead of ordinary IL fallback whenever ZACLR reaches them:
       - IsBitwiseEquatable
       - GetRawData
       - GetUninitializedObject
       - EnumEquals
       - EnumCompareTo
       - AllocTailCallArgBuffer
       - DispatchTailCalls
    */

    static bool text_equals(const char* left, const char* right)
    {
        return zaclr_text_equals(left, right);
    }

    static struct zaclr_stack_value* resolve_local_address_target(struct zaclr_frame* frame,
                                                                  struct zaclr_stack_value* value)
    {
        if (frame == NULL || value == NULL)
        {
            return NULL;
        }

        if (value->kind == ZACLR_STACK_VALUE_BYREF)
        {
            return (value->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u
                ? (struct zaclr_stack_value*)(uintptr_t)value->data.raw
                : NULL;
        }

        return zaclr_dispatch_resolve_local_address_target(frame, value);
    }

    static struct zaclr_result push_i4(struct zaclr_frame* frame, int32_t value)
    {
        return zaclr_dispatch_push_i4(frame, value);
    }

    static struct zaclr_result push_i8(struct zaclr_frame* frame, int64_t value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_I8;
        stack_value.data.i8 = value;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_object_reference(struct zaclr_frame* frame,
                                                     struct zaclr_object_desc* value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        stack_value.data.object_reference = value;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static bool is_unsafe_as_intrinsic(const struct zaclr_type_desc* type,
                                       const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, "As")
            && method->signature.parameter_count == 1u;
    }

    static bool is_unsafe_aspointer_intrinsic(const struct zaclr_type_desc* type,
                                              const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, "AsPointer")
            && method->signature.parameter_count == 1u;
    }

    static bool is_unsafe_add_intrinsic(const struct zaclr_type_desc* type,
                                        const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, "Add")
            && method->signature.parameter_count == 2u;
    }

    static bool is_vector_ishardwareaccelerated_intrinsic(const struct zaclr_type_desc* type,
                                                          const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Numerics")
            && text_equals(type->type_name.text, "Vector")
            && text_equals(method->name.text, "get_IsHardwareAccelerated")
            && method->signature.parameter_count == 0u;
    }

    static bool is_runtimehelpers_getmethodtable_intrinsic(const struct zaclr_type_desc* type,
                                                           const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "GetMethodTable")
            && method->signature.parameter_count == 1u;
    }

    static bool is_runtimehelpers_objecthascomponentsize_intrinsic(const struct zaclr_type_desc* type,
                                                                   const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "ObjectHasComponentSize")
            && method->signature.parameter_count == 1u;
    }

    /*
     * RuntimeHelpers.IsBitwiseEquatable<T>()
     * This is a JIT intrinsic.  Its IL body is `newobj NotSupportedException; throw`, which
     * is never reached by a native JIT compiler.  ZACLR must intercept it before the IL is
     * executed, otherwise the NotSupportedException allocation will OOM the managed heap
     * (the object accumulates with every call since there is no finalizer to reclaim it).
     * We always return false (0) so that callers (e.g. Array.LastIndexOf) take the
     * EqualityComparer<T> slow path, which is fully supported by ZACLR.
     */
    static bool is_runtimehelpers_isbitwiseequatable_intrinsic(const struct zaclr_type_desc* type,
                                                               const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "IsBitwiseEquatable")
            && method->signature.parameter_count == 0u;
    }

    static bool is_methodtable_hasfinalizer_intrinsic(const struct zaclr_type_desc* type,
                                                      const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "MethodTable")
            && text_equals(method->name.text, "get_HasFinalizer")
            && method->signature.parameter_count == 0u;
    }

    static bool is_methodtable_hascomponentsize_intrinsic(const struct zaclr_type_desc* type,
                                                          const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "MethodTable")
            && text_equals(method->name.text, "get_HasComponentSize")
            && method->signature.parameter_count == 0u;
    }

    static bool is_interlocked_exchange_intrinsic(const struct zaclr_type_desc* type,
                                                  const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Threading")
            && text_equals(type->type_name.text, "Interlocked")
            && text_equals(method->name.text, "Exchange");
    }

    static bool is_gchandle_gethandlevalue_intrinsic(const struct zaclr_type_desc* type,
                                                     const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.InteropServices")
            && text_equals(type->type_name.text, "GCHandle")
            && text_equals(method->name.text, "GetHandleValue")
            && method->signature.parameter_count == 1u;
    }

    static bool is_objecthandleonstack_create_intrinsic(const struct zaclr_type_desc* type,
                                                        const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "ObjectHandleOnStack")
            && text_equals(method->name.text, "Create")
            && method->signature.parameter_count == 1u;
    }

    static bool is_monitor_enter_intrinsic(const struct zaclr_type_desc* type,
                                           const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Threading")
            && text_equals(type->type_name.text, "Monitor")
            && text_equals(method->name.text, "Enter")
            && method->signature.parameter_count == 2u;
    }
}

static struct zaclr_result invoke_unsafe_as_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;

    ZACLR_TRACE_VALUE(frame != NULL ? frame->runtime : NULL,
                      ZACLR_TRACE_CATEGORY_EXEC,
                      ZACLR_TRACE_EVENT_CALL_TARGET,
                      "UnsafeAsIntrinsic",
                      1u);

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR][intrinsic] exchange pop value failed status=");
        console_write_dec((uint64_t)result.status);
        console_write("\n");
        return result;
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &value);
}

static struct zaclr_result invoke_unsafe_aspointer_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;
    uintptr_t pointer_value = 0u;

    ZACLR_TRACE_VALUE(frame != NULL ? frame->runtime : NULL,
                      ZACLR_TRACE_CATEGORY_EXEC,
                      ZACLR_TRACE_EVENT_CALL_TARGET,
                      "UnsafeAsPointerIntrinsic",
                      1u);

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_BYREF)
    {
        pointer_value = (uintptr_t)value.data.raw;
    }
    else if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        pointer_value = (uintptr_t)target;
    }
    else if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        pointer_value = (uintptr_t)value.data.object_reference;
    }
    else if (value.kind == ZACLR_STACK_VALUE_I8)
    {
        pointer_value = (uintptr_t)value.data.i8;
    }
    else if (value.kind == ZACLR_STACK_VALUE_I4)
    {
        pointer_value = (uintptr_t)(uint32_t)value.data.i4;
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return push_i8(frame, (int64_t)pointer_value);
}


static struct zaclr_result invoke_unsafe_add_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value index_value = {};
    struct zaclr_stack_value byref_value = {};
    struct zaclr_result result;
    int64_t index = 0;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &index_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &byref_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (byref_value.kind != ZACLR_STACK_VALUE_BYREF && byref_value.kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (index_value.kind == ZACLR_STACK_VALUE_I4)
    {
        index = (int64_t)index_value.data.i4;
    }
    else if (index_value.kind == ZACLR_STACK_VALUE_I8)
    {
        index = index_value.data.i8;
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (byref_value.payload_size == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    byref_value.data.raw = (uintptr_t)((uint8_t*)(uintptr_t)byref_value.data.raw + (index * (int64_t)byref_value.payload_size));
    return zaclr_eval_stack_push(&frame->eval_stack, &byref_value);
}

static struct zaclr_result invoke_vector_ishardwareaccelerated_intrinsic(struct zaclr_frame* frame)
{
    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return push_i4(frame, 0);
}

static struct zaclr_result invoke_runtimehelpers_isbitwiseequatable_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value generic_owner = {};
    const struct zaclr_method_table* method_table = NULL;
    uint8_t result_value = 0u;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    /* CoreCLR does not execute the placeholder IL body for RuntimeHelpers.IsBitwiseEquatable<T>().
       It recognizes the exact RuntimeHelpers method identity in vm/corelib.h and replaces the body in
       vm/jitinterface.cpp:getILIntrinsicImplementationForRuntimeHelpers(). ZACLR mirrors that policy here
       in the interpreter rather than allowing fallback to the dummy managed body.

       Current conservative rule set:
       - primitive scalar value types (1/2/4/8-byte, pointer-sized, char, bool) => true
       - enums => true
       - any reference type / array / string / component-sized type / type containing references => false
       - other value types => false for now until ZACLR gains a full CanCompareBits-style analysis

       This keeps the semantics safe and avoids routing Array.LastIndexOf / MemoryExtensions into the JIT-
       only NotSupported-style path when the runtime is interpreter-first. */
    {
        struct zaclr_result pop_result = zaclr_eval_stack_pop(&frame->eval_stack, &generic_owner);
        if (pop_result.status == ZACLR_STATUS_OK)
        {
            if (generic_owner.kind == ZACLR_STACK_VALUE_I8)
            {
                method_table = (const struct zaclr_method_table*)(uintptr_t)generic_owner.data.i8;
            }
            else if (generic_owner.kind == ZACLR_STACK_VALUE_I4)
            {
                method_table = (const struct zaclr_method_table*)(uintptr_t)(uint32_t)generic_owner.data.i4;
            }
        }
    }

    if (method_table != NULL)
    {
        if (zaclr_method_table_is_enum(method_table) != 0u)
        {
            result_value = 1u;
        }
        else if (zaclr_method_table_is_value_type(method_table) != 0u
                 && zaclr_method_table_contains_references(method_table) == 0u
                 && zaclr_method_table_component_size(method_table) == 0u)
        {
            uint32_t payload_size = zaclr_method_table_instance_size(method_table);
            if (payload_size >= (uint32_t)sizeof(struct zaclr_object_desc))
            {
                payload_size -= (uint32_t)sizeof(struct zaclr_object_desc);
            }
            else
            {
                payload_size = 0u;
            }

            if (payload_size == 1u
                || payload_size == 2u
                || payload_size == 4u
                || payload_size == 8u
                || payload_size == sizeof(uintptr_t))
            {
                result_value = 1u;
            }
        }
    }

    return push_i4(frame, result_value != 0u ? 1 : 0);
}

static struct zaclr_result invoke_runtimehelpers_getmethodtable_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_object_desc* object;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = *target;
    }

    if (value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    object = value.data.object_reference;
    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    {
        const struct zaclr_method_table* method_table = zaclr_object_method_table_const(object);
        return push_i8(frame, method_table != NULL ? (int64_t)(uintptr_t)method_table : 0);
    }
}

static struct zaclr_result invoke_runtimehelpers_objecthascomponentsize_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_object_desc* object;
    const struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = *target;
    }

    if (value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    object = value.data.object_reference;
    if (object == NULL)
    {
        return push_i4(frame, 0);
    }

    method_table = zaclr_object_method_table_const(object);
    return push_i4(frame,
                   method_table != NULL && zaclr_method_table_component_size(method_table) != 0u ? 1 : 0);
}

static struct zaclr_result invoke_methodtable_hasfinalizer_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value method_table_value = {};
    const struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &method_table_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &method_table_value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        method_table_value = *target;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_I8)
    {
        method_table = (const struct zaclr_method_table*)(uintptr_t)method_table_value.data.i8;
    }
    else if (method_table_value.kind != ZACLR_STACK_VALUE_I4)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (method_table == NULL)
    {
        return push_i4(frame, 0);
    }

    return push_i4(frame, zaclr_method_table_has_finalizer(method_table) != 0u ? 1 : 0);
}

static struct zaclr_result invoke_methodtable_hascomponentsize_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value method_table_value = {};
    const struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &method_table_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &method_table_value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        method_table_value = *target;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_I8)
    {
        method_table = (const struct zaclr_method_table*)(uintptr_t)method_table_value.data.i8;
    }
    else if (method_table_value.kind != ZACLR_STACK_VALUE_I4)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (method_table == NULL)
    {
        return push_i4(frame, 0);
    }

    return push_i4(frame, zaclr_method_table_component_size(method_table) != 0u ? 1 : 0);
}

static struct zaclr_result invoke_interlocked_exchange_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR][intrinsic] exchange pop address failed status=");
        console_write_dec((uint64_t)result.status);
        console_write("\n");
        return result;
    }

    console_write("[ZACLR][intrinsic] exchange value_kind=");
    console_write_dec((uint64_t)value.kind);
    console_write(" address_kind=");
    console_write_dec((uint64_t)address.kind);
    console_write(" address_flags=");
    console_write_dec((uint64_t)address.flags);
    console_write(" payload=");
    console_write_dec((uint64_t)address.payload_size);
    console_write(" raw=");
    console_write_hex64((uint64_t)(uintptr_t)address.data.raw);
    console_write("\n");

    if (address.kind == ZACLR_STACK_VALUE_BYREF
        && (address.flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) == 0u)
    {
        raw_address = (void*)(uintptr_t)address.data.raw;
    }
    else
    {
        target = resolve_local_address_target(frame, &address);
    }

    if (target == NULL && raw_address == NULL)
    {
        console_write("[ZACLR][intrinsic] exchange no target resolved\n");
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (raw_address != NULL)
    {
        if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            struct zaclr_object_desc** slot = (struct zaclr_object_desc**)raw_address;
            struct zaclr_object_desc* previous = *slot;
            *slot = value.data.object_reference;
            return push_object_reference(frame, previous);
        }

        if (address.payload_size >= sizeof(int64_t)
            || value.kind == ZACLR_STACK_VALUE_I8)
        {
            int64_t* slot = (int64_t*)raw_address;
            int64_t previous_value = *slot;
            int64_t next_value = value.kind == ZACLR_STACK_VALUE_I8 ? value.data.i8 : (int64_t)value.data.i4;
            *slot = next_value;
            return push_i8(frame, previous_value);
        }

        {
            int32_t* slot = (int32_t*)raw_address;
            int32_t previous_value = *slot;
            int32_t next_value = value.kind == ZACLR_STACK_VALUE_I4 ? value.data.i4 : (int32_t)value.data.raw;
            *slot = next_value;
            return push_i4(frame, previous_value);
        }
    }

    if (target->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE || value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        struct zaclr_stack_value previous = *target;
        target->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        target->data.object_reference = value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE ? value.data.object_reference : NULL;
        return zaclr_eval_stack_push(&frame->eval_stack, &previous);
    }

    if (target->kind == ZACLR_STACK_VALUE_I8 || value.kind == ZACLR_STACK_VALUE_I8)
    {
        int64_t previous_value = target->kind == ZACLR_STACK_VALUE_I8 ? target->data.i8 : (int64_t)target->data.i4;
        int64_t next_value = value.kind == ZACLR_STACK_VALUE_I8 ? value.data.i8 : (int64_t)value.data.i4;
        target->kind = ZACLR_STACK_VALUE_I8;
        target->data.i8 = next_value;
        return push_i8(frame, previous_value);
    }

    {
        int32_t previous_value = target->kind == ZACLR_STACK_VALUE_I4 ? target->data.i4 : (int32_t)target->data.raw;
        int32_t next_value = value.kind == ZACLR_STACK_VALUE_I4 ? value.data.i4 : (int32_t)value.data.raw;
        target->kind = ZACLR_STACK_VALUE_I4;
        target->data.i4 = next_value;
        return push_i4(frame, previous_value);
    }
}

static struct zaclr_result invoke_gchandle_gethandlevalue_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;
    const void* payload = NULL;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = *target;
    }

    if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE && value.data.object_reference != NULL)
    {
        const struct zaclr_boxed_value_desc* boxed_value = (const struct zaclr_boxed_value_desc*)value.data.object_reference;
        if ((zaclr_object_flags(&boxed_value->object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
        {
            value = boxed_value->value;
        }
    }

    if (value.kind == ZACLR_STACK_VALUE_I8)
    {
        return push_i8(frame, value.data.i8 & ~1ll);
    }

    if (value.kind == ZACLR_STACK_VALUE_VALUETYPE)
    {
        payload = zaclr_stack_value_payload_const(&value);
        if (payload == NULL || value.payload_size == 0u)
        {
            return push_i4(frame, 0);
        }

        if (value.payload_size >= sizeof(int64_t))
        {
            int64_t raw = 0;
            const uint8_t* bytes = (const uint8_t*)payload;
            for (uint32_t index = 0u; index < sizeof(int64_t); ++index)
            {
                raw |= ((int64_t)bytes[index]) << (index * 8u);
            }

            return push_i8(frame, raw & ~1ll);
        }

        if (value.payload_size >= sizeof(int32_t))
        {
            int32_t raw = 0;
            const uint8_t* bytes = (const uint8_t*)payload;
            for (uint32_t index = 0u; index < sizeof(int32_t); ++index)
            {
                raw |= (int32_t)((uint32_t)bytes[index] << (index * 8u));
            }

            return push_i4(frame, raw & ~1);
        }

        return push_i4(frame, 0);
    }

    return push_i4(frame, value.data.i4 & ~1);
}

static struct zaclr_result invoke_objecthandleonstack_create_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        if (frame->eval_stack.depth == 0u && frame->local_count > 0u)
        {
            struct zaclr_stack_value synthetic = {};
            result = zaclr_stack_value_set_byref(&synthetic,
                                                 &frame->locals[0],
                                                 sizeof(struct zaclr_stack_value),
                                                 0u,
                                                 ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return zaclr_eval_stack_push(&frame->eval_stack, &synthetic);
        }

        return result;
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &value);
}

static struct zaclr_result invoke_monitor_enter_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value lock_taken_address = {};
    struct zaclr_stack_value object_value = {};
    struct zaclr_stack_value* target = NULL;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &lock_taken_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &object_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (object_value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || object_value.data.object_reference == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    target = resolve_local_address_target(frame, &lock_taken_address);
    if (target == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    target->kind = ZACLR_STACK_VALUE_I4;
    target->data.i4 = 1;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_try_invoke_intrinsic(struct zaclr_frame* frame,
                                                           const struct zaclr_loaded_assembly* assembly,
                                                           const struct zaclr_type_desc* type,
                                                           const struct zaclr_method_desc* method)
{
    const struct zaclr_type_desc* effective_type = type;

    (void)assembly;

    if (effective_type == NULL && assembly != NULL && method != NULL)
    {
        effective_type = zaclr_type_map_find_by_token(&assembly->type_map, method->owning_type_token);
    }

    if (is_unsafe_as_intrinsic(effective_type, method))
    {
        return invoke_unsafe_as_intrinsic(frame);
    }

    if (is_unsafe_aspointer_intrinsic(effective_type, method))
    {
        return invoke_unsafe_aspointer_intrinsic(frame);
    }

    if (is_unsafe_add_intrinsic(effective_type, method))
    {
        return invoke_unsafe_add_intrinsic(frame);
    }

    if (is_vector_ishardwareaccelerated_intrinsic(effective_type, method))
    {
        return invoke_vector_ishardwareaccelerated_intrinsic(frame);
    }

    if (is_runtimehelpers_getmethodtable_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_getmethodtable_intrinsic(frame);
    }

    if (is_runtimehelpers_objecthascomponentsize_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_objecthascomponentsize_intrinsic(frame);
    }

    if (is_runtimehelpers_isbitwiseequatable_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_isbitwiseequatable_intrinsic(frame);
    }

    if (is_methodtable_hasfinalizer_intrinsic(effective_type, method))
    {
        return invoke_methodtable_hasfinalizer_intrinsic(frame);
    }

    if (is_methodtable_hascomponentsize_intrinsic(effective_type, method))
    {
        return invoke_methodtable_hascomponentsize_intrinsic(frame);
    }

    if (is_interlocked_exchange_intrinsic(effective_type, method))
    {
        console_write("[ZACLR][intrinsic] matched Interlocked.Exchange ns=");
        console_write(effective_type != NULL && effective_type->type_namespace.text != NULL ? effective_type->type_namespace.text : "<null>");
        console_write(" type=");
        console_write(effective_type != NULL && effective_type->type_name.text != NULL ? effective_type->type_name.text : "<null>");
        console_write(" method=");
        console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
        console_write(" params=");
        console_write_dec((uint64_t)(method != NULL ? method->signature.parameter_count : 0u));
        console_write(" depth=");
        console_write_dec((uint64_t)(frame != NULL ? frame->eval_stack.depth : 0u));
        console_write("\n");
        return invoke_interlocked_exchange_intrinsic(frame);
    }

    if (is_gchandle_gethandlevalue_intrinsic(effective_type, method))
    {
        return invoke_gchandle_gethandlevalue_intrinsic(frame);
    }

    if (is_objecthandleonstack_create_intrinsic(effective_type, method))
    {
        console_write("[ZACLR][intrinsic] matched ObjectHandleOnStack.Create depth=");
        console_write_dec((uint64_t)(frame != NULL ? frame->eval_stack.depth : 0u));
        console_write(" type_ns=");
        console_write(effective_type != NULL && effective_type->type_namespace.text != NULL ? effective_type->type_namespace.text : "<null>");
        console_write(" type=");
        console_write(effective_type != NULL && effective_type->type_name.text != NULL ? effective_type->type_name.text : "<null>");
        console_write("\n");
        return invoke_objecthandleonstack_create_intrinsic(frame);
    }

    if (is_monitor_enter_intrinsic(effective_type, method))
    {
        return invoke_monitor_enter_intrinsic(frame);
    }

    if (is_memberinfo_get_module_intrinsic(effective_type, method))
    {
        return invoke_memberinfo_get_module_intrinsic(frame);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
}
