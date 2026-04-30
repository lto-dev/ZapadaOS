#include <kernel/zaclr/runtime/zaclr_boot_shared.h>

namespace
{
    static const char* s_command_line = "";
}

extern "C" void zaclr_boot_shared_set_command_line(const char* value)
{
    s_command_line = value != NULL ? value : "";
}

extern "C" const char* zaclr_boot_shared_get_command_line(void)
{
    return s_command_line != NULL ? s_command_line : "";
}
