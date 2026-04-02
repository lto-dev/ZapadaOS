/*
 * Zapada - src/kernel/types.h
 *
 * Basic primitive type definitions for the Zapada native kernel.
 * These avoid any dependency on a C standard library (which does not exist
 * in the bare-metal environment).
 */

#ifndef ZAPADA_TYPES_H
#define ZAPADA_TYPES_H

/* Fixed-width integer types */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* Pointer-sized types (x86-64: 64-bit) */
typedef uint64_t  uintptr_t;
typedef int64_t   intptr_t;
typedef uint64_t  size_t;

/* Null pointer */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* Boolean helpers.
 * In C, mirror what <stdbool.h> would normally provide.
 * In C++, native bool/true/false already exist and must not be redefined.
 */
#ifndef __cplusplus
#define bool  _Bool
#define true  ((_Bool)1)
#define false ((_Bool)0)
#endif

#endif /* ZAPADA_TYPES_H */


