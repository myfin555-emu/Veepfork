// Vita3K substitute adapter
// Original: https://github.com/comex/substitute (LGPLv2.1+)
// Adapted for Vita3K emulator: compile on host, target ARM instruction set.
#pragma once

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// MSVC compatibility
#ifdef _MSC_VER
#include <intrin.h>
#define __builtin_memcpy memcpy
#define __builtin_abort abort
#define __builtin_unreachable() __assume(0)
static __forceinline int __builtin_ctz(unsigned int x) {
    unsigned long index;
    _BitScanForward(&index, x);
    return (int)index;
}
#ifndef UNUSED
#define UNUSED
#endif
#ifndef NOINLINE
#define NOINLINE __declspec(noinline)
#endif
#ifndef INLINE
#define INLINE __forceinline
#endif
#else // GCC/Clang
#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif
#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif
#ifndef INLINE
#define INLINE __attribute__((always_inline)) inline
#endif
#endif

// Force ARM target regardless of host architecture
#define FORCE_TARGET_arm
#define TARGET_arm
#define TARGET_DIR arm

typedef uint32_t uint_tptr;

#define stringify_(x) #x
#define stringify(x) stringify_(x)

#include "arm/misc.h"

// Avoid conflicts: substitute's EXPORT is empty (static library)
#undef EXPORT
#define EXPORT

#define substitute_panic(...) do { \
    fprintf(stderr, "substitute panic: " __VA_ARGS__); \
    abort(); \
    __builtin_unreachable(); \
} while(0)

#define substitute_assert(x) do { \
    if (!(x)) { __builtin_abort(); } \
} while(0)
