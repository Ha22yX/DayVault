# Task 3 Report: Composite CDC+MSC Init at Boot

## Status

**DONE** — `pio run -e dayvault` compiles and links cleanly (SUCCESS). Not flashed (per scope; flash + Windows enumeration is Task 4).

## Summary

Modified the framework core `usbd_cdc_if.c` `CDC_init()` so the device registers a CDC+MSC composite
(via `USBD_RegisterClassComposite`) instead of a single CDC class. `Serial.begin(115200)` already calls
`CDC_init()` at boot, so no app changes were needed. The MSC storage symbol is the C-linkage
`USBD_MSC_Storage_fops` exported by `firmware/src/UsbMscStorage.cpp` (Task 1).

## Verified Symbols (all present, correct signatures)

- `USBD_CDC` (`USBD_ClassTypeDef`, `libraries/USBDevice/src/cdc/usbd_cdc.c:140`)
- `USBD_MSC` (`USBD_ClassTypeDef`, `system/Middlewares/.../Class/MSC/Src/usbd_msc.c:105`)
- `USBD_RegisterClassComposite` (`USBD_StatusTypeDef (USBD_HandleTypeDef*, USBD_ClassTypeDef*, USBD_CompositeClassTypeDef, uint8_t*)`, `.../Core/Src/usbd_core.c:246`)
- `CLASS_TYPE_CDC = 2`, `CLASS_TYPE_MSC = 3` (`.../Core/Inc/usbd_def.h:321-322`)
- `USBD_MSC_RegisterStorage` (`uint8_t (USBD_HandleTypeDef*, USBD_StorageTypeDef*)`, `firmware/src/usbd_msc.c:554`)
- `USBD_MSC_Storage_fops` (C linkage, `firmware/src/UsbMscStorage.cpp:77`)
- Build flags from Task 2 (`USE_USBD_COMPOSITE=1`, `USBD_CMPSIT_ACTIVATE_CDC=1`, `USBD_CMPSIT_ACTIVATE_MSC=1`, `USBD_COMPOSITE_USE_IAD=1`, `USBD_MAX_NUM_INTERFACES=3`) confirmed in `platformio.ini`.

No symbol was missing or renamed; no BLOCKED condition.

## File Modified

`C:\Users\Administrator\.platformio\packages\framework-arduinoststm32\libraries\USBDevice\src\cdc\usbd_cdc_if.c`

- Added `#include "usbd_msc.h"` and `extern USBD_StorageTypeDef USBD_MSC_Storage_fops;` at the top (near the other includes/externs).
- Replaced the `USBD_RegisterClass(&hUSBD_Device_CDC, USBD_CDC_CLASS)` body of `CDC_init()` with the composite registration.

## Exact New `CDC_init()` Body

```c
void CDC_init(void)
{
#if defined(ICACHE) && defined (HAL_ICACHE_MODULE_ENABLED) && !defined(HAL_ICACHE_MODULE_DISABLED)
  if (HAL_ICACHE_IsEnabled() == 1) {
    icache_enabled = true;
    /* Disable instruction cache prior to internal cacheable memory update */
    if (HAL_ICACHE_Disable() != HAL_OK) {
      Error_Handler();
    }
  }
#endif /* ICACHE && HAL_ICACHE_MODULE_ENABLED && !HAL_ICACHE_MODULE_DISABLED */
  if (!CDC_initialized) {
    /* Init Device Library */
    if (USBD_Init(&hUSBD_Device_CDC, &USBD_Desc, 0) == USBD_OK) {
      /* Add Supported Class (CDC+MSC composite) */
      uint8_t cdcEps[3] = {0x81, 0x01, 0x82};   /* CDC: IN-bulk, OUT-bulk, IN-intr(CMD) */
      if (USBD_RegisterClassComposite(&hUSBD_Device_CDC, &USBD_CDC, CLASS_TYPE_CDC, cdcEps) == USBD_OK) {
        /* Add CDC Interface Class */
        if (USBD_CDC_RegisterInterface(&hUSBD_Device_CDC, &USBD_CDC_fops) == USBD_OK) {
          /* Add MSC class */
          uint8_t mscEps[2] = {0x83, 0x03};     /* MSC: IN-bulk, OUT-bulk */
          if (USBD_RegisterClassComposite(&hUSBD_Device_CDC, &USBD_MSC, CLASS_TYPE_MSC, mscEps) == USBD_OK) {
            /* Add MSC Storage Interface */
            if (USBD_MSC_RegisterStorage(&hUSBD_Device_CDC, &USBD_MSC_Storage_fops) == USBD_OK) {
              /* Start Device Process */
              USBD_Start(&hUSBD_Device_CDC);
              CDC_initialized = true;
            }
          }
        }
      }
    }
  }
}
```

Only `CDC_init()` was changed. The ICACHE guard and `CDC_deInit` are untouched.

## Compile Result

Command (from `C:\Users\Administrator\Desktop\DayVault\firmware`):

- `pio run -e dayvault` after `pio run -t clean -e dayvault`: **SUCCESS** (both clean and incremental).
- Only pre-existing warnings remain (unchanged, not from this task):
  - `generic_clock.c:28` `#warning "SystemClock_Config() is empty..."`
  - linker `LOAD segment with RWX permissions`

## RAM / Flash Usage

- RAM: **85.8%** — 56212 / 65536 bytes (64 KB total). (Task 2 baseline: 85.5% / 56052 B; +~160 B)
- Flash: **24.5%** — 64096 / 262144 bytes (256 KB total). (Task 2 baseline: 22.5% / 59032 B; +~5.0 KB)

The delta comes from wiring the composite builder/MSC class objects into the active build path and the
added registration calls in `CDC_init()`.

## Constraints Compliance

- Did NOT modify FLASH option bytes (no dfu-util, no option-byte writes).
- Audio pipeline untouched.
- MSC read-only policy unchanged (`UsbMscStorage.cpp` returns write-protected).
- Did NOT modify `firmware/src/UsbMscStorage.*`.
- Did NOT change app code (`main.cpp` / setup); composite starts via existing `Serial.begin(115200)` → `CDC_init()`.
- Did NOT flash.

## Concerns

1. **Runtime enumeration is unverified (Task 4).** This task is compile-only. Windows enumeration of the
   CDC+MSC composite, MSC read-only mount, and coexistence with CDC serial must be validated by flashing
   in Task 4. If enumeration fails, first suspects are descriptor sizing and endpoint allocation done by the
   composite builder at runtime (not statically checkable here).
2. **Framework patch is invisible to git.** `usbd_cdc_if.c` (and the Task 1/2 framework patches to
   `usbd_cdc.c`, `usbd_desc.c`) live under `.platformio/packages/framework-arduinoststm32`, which is not
   tracked by the DayVault repo. A PlatformIO reinstall/upgrade will silently revert these patches. The
   re-provisioning patch list (documented as a deferred item in progress.md) remains pending for Task 4/docs.
3. **RAM pressure (85.8%).** Only ~9.3 KB RAM free; the added USB composite path is small, but Task 4
   should confirm no runtime growth.
4. **Endpoint mapping.** The composite uses CDC EP 0x81/0x01/0x82 and MSC EP 0x83/0x03. These differ from
   the STM32duino defaults (`usbd_ep_conf.h` CDC_OUT_EP=0x01, CDC_IN_EP=0x82, CDC_CMD_EP=0x83); the
   composite builder assigns endpoints from the arrays passed at registration, so this is intentional, but
   it must be confirmed at enumeration.
