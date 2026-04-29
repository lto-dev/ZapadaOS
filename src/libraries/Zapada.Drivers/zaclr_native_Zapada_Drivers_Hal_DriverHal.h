#ifndef ZACLR_NATIVE_ZAPADA_DRIVERS_HAL_DRIVERHAL_H
#define ZACLR_NATIVE_ZAPADA_DRIVERS_HAL_DRIVERHAL_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Drivers_Hal_DriverHal
{
    static struct zaclr_result CreateChannel___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result DestroyChannel___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result TrySend___STATIC__I4__I4__I4__SZARRAY_U1__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result TryReceive___STATIC__I4__I4__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame);

    static struct zaclr_result PciFindDevice___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PciDeviceCount___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PciGetDeviceInfo___STATIC__I4__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PciReadConfig32___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PciReadBar32___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PciOpenBar___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);

    static struct zaclr_result MmioRead32___STATIC__I4__I8__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result MmioWrite32___STATIC__I4__I8__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result MmioRegionSize___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result MmioRegionRead32___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result MmioRegionWrite32___STATIC__I4__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result CloseMmioRegion___STATIC__I4__I4(struct zaclr_native_call_frame& frame);

    static struct zaclr_result AllocBuffer___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result FreeBuffer___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result BufferSize___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
};

#endif /* ZACLR_NATIVE_ZAPADA_DRIVERS_HAL_DRIVERHAL_H */
