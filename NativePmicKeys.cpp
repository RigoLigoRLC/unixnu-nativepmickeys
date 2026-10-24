// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "NativePmicKeys.h"
#include "NativeQcomSpmi.h"
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/mach_time.h>

namespace {
struct KeyState {
    NativePmicKey config;
    bool candidate;
    bool stable;
    uint64_t changedAt;
};

struct KeyPeripheral {
    uint8_t sid;
    uint8_t peripheral;
    uint8_t bits;
    uint8_t value;
};

static KeyState states[8];
static KeyPeripheral peripherals[8];
static unsigned keyCount, peripheralCount;
static NativePmicKeysReport reportSink;
static void *reportContext;
static uint32_t startState, events, reports, errors, pressed;
static int lastError;

static uint64_t nowUs()
{
    uint64_t nanoseconds = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &nanoseconds);
    return nanoseconds / 1000;
}

static void notify(void *)
{
    __atomic_fetch_add(&events, 1u, __ATOMIC_RELEASE);
    thread_wakeup(reinterpret_cast<event_t>(&events));
}

static int readInputs()
{
    for (unsigned index = 0; index < peripheralCount; ++index) {
        KeyPeripheral &peripheral = peripherals[index];
        native_qcom_spmi_irq_mask(peripheral.sid, peripheral.peripheral);
        int result = native_qcom_spmi_irq_ack(peripheral.sid, peripheral.peripheral);
        if (!result) {
            result = native_qcom_spmi_read(peripheral.sid,
                (peripheral.peripheral << 8) | 0x10, &peripheral.value, 1);
        }
        if (result) {
            return result;
        }
    }
    return 0;
}

static void worker(void *, wait_result_t)
{
    thread_set_thread_name(current_thread(), "pmic-keys");
    bool first = true;
    uint32_t lastReported = 0;
    for (;;) {
        uint32_t seen = __atomic_load_n(&events, __ATOMIC_ACQUIRE);
        int result = readInputs();
        if (result) {
            lastError = result;
            if (!(errors++ % 64)) {
                printf("NativePmicKeys: read error=%d count=%u\n", result, errors);
            }
            // A failed bus read leaves the last accepted state intact for retry.
            IOSleep(20);
            continue;
        }
        bool settling = false;
        uint64_t now = nowUs();
        uint32_t mask = 0;
        for (unsigned index = 0; index < keyCount; ++index) {
            KeyState &key = states[index];
            bool down = false;
            for (unsigned item = 0; item < peripheralCount; ++item) {
                KeyPeripheral &peripheral = peripherals[item];
                if (peripheral.sid == key.config.sid && peripheral.peripheral == key.config.peripheral) {
                    down = ((peripheral.value & key.config.statusBit) != 0) != key.config.activeLow;
                    break;
                }
            }
            if (first || down != key.candidate) {
                key.candidate = down;
                key.changedAt = now;
            }
            if (key.candidate != key.stable) {
                if (now - key.changedAt >= key.config.debounceUs) {
                    key.stable = key.candidate;
                } else {
                    settling = true;
                }
            }
            if (key.stable) {
                mask |= key.config.reportBit;
            }
        }
        first = false;
        pressed = mask;
        if (mask != lastReported) {
            // Keep an accepted DOWN until the consumer accepts it; a later UP
            // is sampled afterward and cannot overwrite the pending report.
            while ((result = reportSink(reportContext, mask)) != 0) {
                lastError = result;
                IOSleep(5);
            }
            printf("NativePmicKeys: report=%u pressed=%x down=%x up=%x\n",
                ++reports, mask, mask & ~lastReported, lastReported & ~mask);
            lastReported = mask;
        }
        if (settling) {
            IOSleep(5);
            continue;
        }
        for (unsigned index = 0; index < peripheralCount; ++index) {
            native_qcom_spmi_irq_rearm(peripherals[index].sid, peripherals[index].peripheral);
        }
        assert_wait(reinterpret_cast<event_t>(&events), THREAD_UNINT);
        if (__atomic_load_n(&events, __ATOMIC_ACQUIRE) != seen) {
            clear_wait(current_thread(), THREAD_AWAKENED);
        } else {
            thread_block(THREAD_CONTINUE_NULL);
        }
    }
}
} // namespace

extern "C" int native_pmic_keys_start(const NativePmicKey *keys, unsigned count,
    NativePmicKeysReport report, void *context)
{
    if (!keys || !count || count > 8 || !report) {
        return -22;
    }
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&startState, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return -16;
    }
    keyCount = count;
    reportSink = report;
    reportContext = context;
    for (unsigned index = 0; index < count; ++index) {
        states[index].config = keys[index];
        unsigned item = 0;
        for (; item < peripheralCount; ++item) {
            if (peripherals[item].sid == keys[index].sid && peripherals[item].peripheral == keys[index].peripheral) {
                break;
            }
        }
        if (item == peripheralCount) {
            peripherals[item].sid = keys[index].sid;
            peripherals[item].peripheral = keys[index].peripheral;
            ++peripheralCount;
        }
        peripherals[item].bits |= keys[index].statusBit;
    }
    for (unsigned index = 0; index < peripheralCount; ++index) {
        KeyPeripheral &peripheral = peripherals[index];
        int result = native_qcom_spmi_irq_register(peripheral.sid, peripheral.peripheral,
            peripheral.bits, notify, nullptr);
        if (result) {
            lastError = result;
            startState = 3;
            return result;
        }
    }
    thread_t thread = THREAD_NULL;
    kern_return_t result = kernel_thread_start(worker, nullptr, &thread);
    if (result == KERN_SUCCESS) {
        thread_deallocate(thread);
        startState = 2;
        return 0;
    }
    startState = 3;
    return -12;
}

extern "C" void native_pmic_keys_dump(void)
{
    printf("NativePmicKeys: stage=%u events=%u reports=%u pressed=%x errors=%u last=%d\n",
        startState, events, reports, pressed, errors, lastError);
}
