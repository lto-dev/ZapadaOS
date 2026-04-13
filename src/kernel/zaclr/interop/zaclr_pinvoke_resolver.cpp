#include <kernel/zaclr/interop/zaclr_pinvoke_resolver.h>

namespace
{
    static constexpr uint16_t k_method_impl_flag_internal_call = 0x1000u;

    static bool text_equals(const char* left, const char* right)
    {
        uint32_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return false;
        }

        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return left[index] == right[index];
    }

    static bool text_contains(const char* text, const char* needle)
    {
        uint32_t start = 0u;
        uint32_t match_index;

        if (text == NULL || needle == NULL || needle[0] == '\0')
        {
            return false;
        }

        while (text[start] != '\0')
        {
            match_index = 0u;
            while (needle[match_index] != '\0'
                && text[start + match_index] != '\0'
                && text[start + match_index] == needle[match_index])
            {
                ++match_index;
            }

            if (needle[match_index] == '\0')
            {
                return true;
            }

            ++start;
        }

        return false;
    }
}

extern "C" struct zaclr_result zaclr_classify_method_dispatch(const struct zaclr_method_desc* method,
                                                               struct zaclr_method_dispatch_info* out_info)
{
    if (method == NULL || out_info == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    out_info->kind = ZACLR_DISPATCH_KIND_NOT_IMPLEMENTED;
    out_info->qcall_entry_point = NULL;
    out_info->pinvoke_module = NULL;
    out_info->pinvoke_import = NULL;

    if (method->rva != 0u && (method->impl_flags & k_method_impl_flag_internal_call) == 0u)
    {
        out_info->kind = ZACLR_DISPATCH_KIND_IL_BODY;
        return zaclr_result_ok();
    }

    if ((method->impl_flags & k_method_impl_flag_internal_call) != 0u)
    {
        out_info->kind = ZACLR_DISPATCH_KIND_INTERNAL_CALL;
        return zaclr_result_ok();
    }

    if (method->pinvoke_import_name.text != NULL)
    {
        out_info->pinvoke_import = method->pinvoke_import_name.text;
        out_info->pinvoke_module = method->pinvoke_module_name.text;

        if (text_equals(method->pinvoke_module_name.text, "QCall")
            || text_contains(method->pinvoke_module_name.text, "QCall"))
        {
            out_info->kind = ZACLR_DISPATCH_KIND_QCALL;
            out_info->qcall_entry_point = method->pinvoke_import_name.text;
            return zaclr_result_ok();
        }

        out_info->kind = ZACLR_DISPATCH_KIND_PINVOKE;
        return zaclr_result_ok();
    }

    out_info->kind = ZACLR_DISPATCH_KIND_NOT_IMPLEMENTED;
    return zaclr_result_ok();
}
