// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NativePmicKey describes one PMIC real-time input and its report bit.
 * The board selects the wiring and debounce duration. The driver combines
 * stable states into one pressed mask and delivers every down/up transition.
 */
struct NativePmicKey {
    uint8_t sid;
    uint8_t peripheral;
    uint8_t statusBit;
    bool activeLow;
    uint32_t reportBit;
    uint32_t debounceUs;
};

typedef int (*NativePmicKeysReport)(void *context, uint32_t pressed);
int native_pmic_keys_start(const struct NativePmicKey *keys, unsigned count,
    NativePmicKeysReport report, void *context);
void native_pmic_keys_dump(void);

#ifdef __cplusplus
}
#endif
