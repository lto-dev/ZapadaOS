#include <kernel/zaclr/runtime/zaclr_boot_shared.h>

namespace
{
    static int32_t s_boot_part_lba = 0;
    static const char* s_command_line = "";
}

extern "C" void zaclr_boot_shared_set_boot_part_lba(int32_t value)
{
    s_boot_part_lba = value;
}

extern "C" int32_t zaclr_boot_shared_get_boot_part_lba(void)
{
    return s_boot_part_lba;
}

extern "C" void zaclr_boot_shared_set_command_line(const char* value)
{
    s_command_line = value != NULL ? value : "";
}

extern "C" const char* zaclr_boot_shared_get_command_line(void)
{
    return s_command_line != NULL ? s_command_line : "";
}
