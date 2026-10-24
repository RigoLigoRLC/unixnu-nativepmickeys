# NativePmicKeys

NativePmicKeys reads PMIC real-time input bits through NativeQcomSpmi and delivers
complete pressed masks to a board-supplied report callback. Each key supplies its
SPMI slave, peripheral, bit, polarity, report bit and debounce duration.

The driver combines keys that share a peripheral into one dual-edge interrupt
registration. Its worker acknowledges the peripheral and reads real-time state,
requires each changed input to remain stable for its configured duration, and
reports all accepted down/up transitions. Sampling every five milliseconds is
limited to debounce or report retry; stable inputs sleep until an interrupt.
Read failures preserve the last accepted state and retry after twenty
milliseconds. A failed report remains pending for retry.

The device repository owns PMIC pin configuration and HID service creation.
SM-T733 registers PMK8350 PON power and volume down, plus PM7325 GPIO6 volume up,
in `package/T733PmicKeys.cpp`. `T733PmicKeysHid.cpp` passes the mask through the
original IOHIDFamily Consumer Control report.

Compile with XNU IOKit and kernel headers, then link NativeQcomSpmi. The driver
and its registrations live for the boot lifetime. License: BSD-2-Clause-Patent.

SM-T733 execution on 2026-10-24 delivered four complete volume-up presses,
four volume-down presses and two power-key presses through the original HID
driver. The capture contained 20 accepted down/up reports and zero input errors.
The PMK8350 PBS debounce byte was `0x05`, corresponding to 1953 microseconds.
