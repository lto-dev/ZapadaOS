#include "zaclr_native_Zapada_Kernel.h"

#include <kernel/zaclr/runtime/zaclr_boot_shared.h>

struct zaclr_result zaclr_native_Zapada_Fs_Fat32_Kernel::GetBootPartLba___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, zaclr_boot_shared_get_boot_part_lba());
}
