#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <float.h>

/* Compile-only ABI probe.
 *
 * This file is meant to make target-width and ABI assumptions explicit in CI.
 *
 */

#define ABI_BITS(T) ((int)(sizeof(T) * CHAR_BIT))

/* General assumptions that should hold on all supported targets. */
_Static_assert(CHAR_BIT == 8, "heatshrink requires 8-bit bytes");
_Static_assert(sizeof(char) == 1, "char must be exactly 1 byte");
_Static_assert(UCHAR_MAX == 0xffu, "unsigned char must be 8-bit");

_Static_assert(ABI_BITS(short) >= 16, "short must be at least 16-bit");
_Static_assert(ABI_BITS(int) >= 16, "int must be at least 16-bit");
_Static_assert(ABI_BITS(long) >= 32, "long must be at least 32-bit");
_Static_assert(ABI_BITS(long long) >= 64, "long long must be at least 64-bit");

_Static_assert(ABI_BITS(uint8_t) == 8, "uint8_t must be exactly 8-bit");
_Static_assert(ABI_BITS(uint16_t) == 16, "uint16_t must be exactly 16-bit");
_Static_assert(ABI_BITS(uint32_t) == 32, "uint32_t must be exactly 32-bit");
_Static_assert(ABI_BITS(uint64_t) == 64, "uint64_t must be exactly 64-bit");
_Static_assert(ABI_BITS(int8_t) == 8, "int8_t must be exactly 8-bit");
_Static_assert(ABI_BITS(int16_t) == 16, "int16_t must be exactly 16-bit");
_Static_assert(ABI_BITS(int32_t) == 32, "int32_t must be exactly 32-bit");
_Static_assert(ABI_BITS(int64_t) == 64, "int64_t must be exactly 64-bit");

_Static_assert(ABI_BITS(size_t) >= ABI_BITS(void *), "size_t must be able to represent pointer-sized objects");
_Static_assert(sizeof(float) >= 4, "float unexpectedly narrow");
_Static_assert(sizeof(double) >= sizeof(float), "double narrower than float");
_Static_assert(FLT_RADIX == 2, "non-binary floating point not expected");

/* Target profile expectations.
 *
 * Keep these target-specific so we do not accidentally exclude other valid
 * ABIs in the future.
 */
#if defined(__AVR__)
#define ABI_EXPECT_SHORT_BITS        16
#define ABI_EXPECT_INT_BITS          16
#define ABI_EXPECT_LONG_BITS         32
#define ABI_EXPECT_LLONG_BITS        64
#define ABI_EXPECT_PTR_BITS          16
#define ABI_EXPECT_SIZE_T_BITS       16
#define ABI_EXPECT_FLOAT_BITS        32
#define ABI_EXPECT_DOUBLE_BITS       32
#define ABI_EXPECT_UINT_FAST8_BITS    8
#define ABI_EXPECT_UINT_FAST16_BITS  16
#define ABI_EXPECT_UINT_FAST32_BITS  32
#endif

#if defined(__MSP430__)
#define ABI_EXPECT_SHORT_BITS        16
#define ABI_EXPECT_INT_BITS          16
#define ABI_EXPECT_LONG_BITS         32
#define ABI_EXPECT_LLONG_BITS        64
#define ABI_EXPECT_PTR_BITS          16
#define ABI_EXPECT_SIZE_T_BITS       16
#define ABI_EXPECT_FLOAT_BITS        32
#define ABI_EXPECT_DOUBLE_BITS       32
#define ABI_EXPECT_UINT_FAST8_BITS   8
#define ABI_EXPECT_UINT_FAST16_BITS  16
#define ABI_EXPECT_UINT_FAST32_BITS  32
#endif

#if defined(__linux__) && defined(__i386__)
#define ABI_EXPECT_SHORT_BITS        16
#define ABI_EXPECT_INT_BITS          32
#define ABI_EXPECT_LONG_BITS         32
#define ABI_EXPECT_LLONG_BITS        64
#define ABI_EXPECT_PTR_BITS          32
#define ABI_EXPECT_SIZE_T_BITS       32
#define ABI_EXPECT_FLOAT_BITS        32
#define ABI_EXPECT_DOUBLE_BITS       64
#define ABI_EXPECT_UINT_FAST8_BITS   8
#define ABI_EXPECT_UINT_FAST16_BITS  32
#define ABI_EXPECT_UINT_FAST32_BITS  32
#endif

#if defined(__linux__) && defined(__x86_64__)
#define ABI_EXPECT_SHORT_BITS        16
#define ABI_EXPECT_INT_BITS          32
#define ABI_EXPECT_LONG_BITS         64
#define ABI_EXPECT_LLONG_BITS        64
#define ABI_EXPECT_PTR_BITS          64
#define ABI_EXPECT_SIZE_T_BITS       64
#define ABI_EXPECT_FLOAT_BITS        32
#define ABI_EXPECT_DOUBLE_BITS       64
#define ABI_EXPECT_UINT_FAST8_BITS   8
#define ABI_EXPECT_UINT_FAST16_BITS  64
#define ABI_EXPECT_UINT_FAST32_BITS  64
#endif

_Static_assert(ABI_BITS(short) == ABI_EXPECT_SHORT_BITS, "unexpected short width for target profile");
_Static_assert(ABI_BITS(int) == ABI_EXPECT_INT_BITS, "unexpected int width for target profile");
_Static_assert(ABI_BITS(long) == ABI_EXPECT_LONG_BITS, "unexpected long width for target profile");
_Static_assert(ABI_BITS(long long) == ABI_EXPECT_LLONG_BITS, "unexpected long long width for target profile");
_Static_assert(ABI_BITS(void *) == ABI_EXPECT_PTR_BITS, "unexpected pointer width for target profile");
_Static_assert(ABI_BITS(size_t) == ABI_EXPECT_SIZE_T_BITS, "unexpected size_t width for target profile");
_Static_assert(ABI_BITS(uint_fast8_t) == ABI_EXPECT_UINT_FAST8_BITS, "unexpected uint_fast8_t width for target profile");
_Static_assert(ABI_BITS(uint_fast16_t) == ABI_EXPECT_UINT_FAST16_BITS, "unexpected uint_fast16_t width for target profile");
_Static_assert(ABI_BITS(uint_fast32_t) == ABI_EXPECT_UINT_FAST32_BITS, "unexpected uint_fast32_t width for target profile");

_Static_assert(ABI_BITS(float) == ABI_EXPECT_FLOAT_BITS, "unexpected float width for target profile");
_Static_assert(ABI_BITS(double) == ABI_EXPECT_DOUBLE_BITS, "unexpected double width for target profile");

int abi_probe(void) {
    return 0;
}
