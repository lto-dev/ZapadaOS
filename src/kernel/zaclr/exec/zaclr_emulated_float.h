#ifndef ZACLR_EMULATED_FLOAT_H
#define ZACLR_EMULATED_FLOAT_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#if defined(PLATFORM_EMULATED_FLOATINGPOINT)

namespace zaclr::emulated_float
{
    constexpr int32_t k_float32_shift = 10;
    constexpr int32_t k_float64_shift = 16;

    static inline int64_t decode_r4_to_common(uint32_t bits)
    {
        uint32_t mantissa = (bits & 0x007FFFFFu) | 0x00800000u;
        int32_t exponent = (int32_t)((bits >> 23) & 0xFFu) - 127;
        int32_t result;

        exponent -= (23 - k_float32_shift);

        if (bits == 0u)
        {
            result = 0;
        }
        else if (exponent <= -31)
        {
            result = 0;
        }
        else if (exponent >= 31)
        {
            result = 0x7FFFFFFF;
        }
        else if (exponent > 0)
        {
            uint64_t temp = ((uint64_t)mantissa) << exponent;
            result = (temp >> 31) != 0u ? 0x7FFFFFFF : (int32_t)temp;
        }
        else if (exponent < 0)
        {
            result = (int32_t)(mantissa >> (-exponent));
        }
        else
        {
            result = (int32_t)mantissa;
        }

        if ((bits & 0x80000000u) != 0u)
        {
            result = -result;
        }

        return ((int64_t)result) << (k_float64_shift - k_float32_shift);
    }

    static inline int64_t decode_r8_to_common(uint64_t bits)
    {
        uint64_t mantissa = (bits & 0x000FFFFFFFFFFFFFULL) | 0x0010000000000000ULL;
        int32_t exponent = (int32_t)((bits >> 52) & 0x7FFu) - 1023;
        uint64_t mask = 0xFFFFFFFFFFFFFFFFULL;
        int64_t result;

        exponent -= (52 - k_float64_shift);

        if (bits == 0u)
        {
            result = 0;
        }
        else if (exponent <= -63)
        {
            result = 0;
        }
        else if (exponent >= 63)
        {
            result = 0x7FFFFFFFFFFFFFFFLL;
        }
        else if (exponent > 0)
        {
            mask <<= (63 - exponent);
            result = (mask & mantissa) != 0u ? 0x7FFFFFFFFFFFFFFFLL : (int64_t)(mantissa << exponent);
        }
        else if (exponent < 0)
        {
            result = (int64_t)(mantissa >> (-exponent));
        }
        else
        {
            result = (int64_t)mantissa;
        }

        if ((bits & 0x8000000000000000ULL) != 0u)
        {
            result = -result;
        }

        return result;
    }

    static inline int64_t multiply_common(int64_t left_common, int64_t right_common)
    {
        uint64_t op1 = (uint64_t)(left_common < 0 ? -left_common : left_common);
        uint64_t op2 = (uint64_t)(right_common < 0 ? -right_common : right_common);
        uint64_t acc = 0u;
        uint64_t part;
        bool negative = ((left_common < 0) ^ (right_common < 0));

        part = op1 * (uint16_t)(op2 >> 0);
        acc += part >> k_float64_shift;
        part = op1 * (uint16_t)(op2 >> 16);
        acc += part;
        part = op1 * (uint16_t)(op2 >> 32);
        acc += part << 16;
        part = op1 * (uint16_t)(op2 >> 48);
        acc += part << 32;
        return negative && acc != 0u ? -(int64_t)acc : (int64_t)acc;
    }
}

#endif

#endif
