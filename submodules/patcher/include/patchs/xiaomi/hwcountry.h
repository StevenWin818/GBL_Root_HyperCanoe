#ifndef XIAOMI_HWCOUNTRY_H
#define XIAOMI_HWCOUNTRY_H
#include <stdint.h>
#include <stdbool.h>
/*
 * Xiaomi HwCountry cross-region patch.
 *
 * Rewrites the ABL HwCountry value to "GLOBAL" for both the fastboot
 * `getvar hwcountry` path and the display path, so a cross-region
 * (e.g. EEA firmware on a CN device) bootloader reports the global
 * region code.
 *
 * Gated: an image without the "HwCountry: %a" format string is not a
 * Xiaomi ABL carrying this code path, so the call is a no-op returning
 * true. Returns false only when the format string exists but locating
 * or rewriting the references fails.
 */
bool patch_xiaomi_hwcountry(char* buffer, int32_t size, uint64_t load_base);
#endif /* XIAOMI_HWCOUNTRY_H */
