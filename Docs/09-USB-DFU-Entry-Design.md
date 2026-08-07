# USB DFU Auto-Entry Design

Date: 2026-08-07
Status: Approved (pending implementation)
Scope: Software-triggered entry into the STM32L452 system-memory USB DFU bootloader from application firmware, controlled over USB CDC without touching BOOT/RESET buttons.

## 1. Motivation

The hardware selects ROM USB DFU only by holding BOOT0 high during reset (Docs/02, Docs/03).
BOOT0 is connected only to a pull-down resistor R3 and button SW2 — it has **no MCU GPIO
connection** (Docs/08-Net-Map: `$1N47` = R3.1; SW2.1; U5.60). Firmware therefore cannot
drive BOOT0. Manual DFU entry (hold BOOT, press RESET, release BOOT) is error-prone during
development.

Goal: after the application enumerates as a USB CDC device, the host sends one line command
and firmware safely jumps into the STM32 ROM bootloader, which re-enumerates as a USB DFU
device. STM32CubeProgrammer then connects over USB DFU with no button presses.

This addresses the "normal firmware can later implement a command that jumps to ROM DFU"
note in Docs/06 DV-015.

## 2. Approach

**Software jump to system memory bootloader (ST AN2606 supported technique), no flash or
option-byte writes.**

- STM32L452 system memory starts at `0x1FFF0000`.
- Reset vector layout at that base: offset `0x0000` = initial MSP, offset `0x0004` = entry PC.
- The ROM bootloader probes connected interfaces and enumerates as USB DFU when it detects
  the host on USB. After programming completes the host resets the part and the application
  boots normally again.
- No option bytes are written, so there is zero risk of bricking; a watchdog or power cycle
  always returns to the application.

## 3. Trigger

USB CDC line-based protocol (matches the pure-logic parser already designed for the
firmware, `usbproto`): a received line equal to `DFU` (optionally with `\r\n`) invokes
`dfu_enter()`.

## 4. `dfu_enter()` Sequence

1. **Stop acquisition** — stop PDM/DFSDM capture (hardware-gated hook; no-op until the
   recording pipeline exists).
2. **Close the current WAV segment** — finalize header, `f_sync`, `f_close`.
3. **Unmount storage** — `f_mount(NULL, "SD:", 0)` (no-op until FatFs is integrated).
4. **Brief settle delay** — allow the last sector write to land (~20 ms).
5. **Tear down peripherals** — `HAL_DeInit()`, stop SysTick, disable all interrupts
   (`__disable_irq()`), reset USB peripheral clocks.
6. **Validate the bootloader vector table** — require
   `(*(volatile uint32_t *)0x1FFF0000 & 0xFFF00000u) == 0x20000000u` (initial MSP points into
   RAM). Abort otherwise.
7. **Jump** — `__set_MSP(*(volatile uint32_t *)0x1FFF0000);` then call
   `(void (*)(void))*(volatile uint32_t *)0x1FFF0004`. No return expected.

## 5. Interface

```c
/* dfu.h */
void dfu_enter(void);
```

`dfu_enter` is intentionally hardware-coupled and compile-only verified; the safe-stop
hooks (acquisition, WAV close, SD unmount) are injected as callbacks so the same entry path
works once the recording pipeline lands:

```c
typedef struct
{
    void (*stop_acquisition)(void);
    void (*close_segment)(void);
    void (*unmount_storage)(void);
} dfu_stop_hooks_t;

void dfu_enter_with_hooks(const dfu_stop_hooks_t *hooks);
```

## 6. Safety

- No flash erase/program, no option-byte modification → cannot brick the device.
- Abort jump if the bootloader stack pointer is invalid.
- All recording storage writes are synced/closed before the jump.
- The application can be restored at any time by a normal reset.

## 7. Testing Strategy

| Layer | Verification |
| --- | --- |
| `usbproto` (pure logic) | host test: line `DFU` (and `DFU\r\n`) produces the enter-DFU event; unknown lines do not. |
| `dfu_enter` (hardware) | compile-only; abort path when vector-table check fails. |
| Board bring-up (Docs/05) | enumerate CDC → send `DFU\n` → STM32CubeProgrammer detects USB DFU → program → reset → application runs. |

## 8. Scope of This Iteration

This design is the first milestone of the firmware rework. Current implementation scope:

- PlatformIO project skeleton, clock tree with USB FS 48 MHz.
- USB CDC enumeration.
- CDC line parsing with `DFU` command.
- `dfu_enter` with injected no-op stop hooks.
- Native host tests for the line parser.

Recording pipeline (RTC, battery ADC, PDM/DFSDM, microSD/FatFs, WAV) is out of scope now;
the hooks above are the integration points for later milestones.
