# USB MSC (U-Disk Export) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When USB is attached (idle, not recording), the DayVault enumerates as a USB Mass Storage Class device exposing the microSD card as a read-only disk, so Windows can browse/copy the REC###.WAV files directly.

**Architecture:** Register the ST USB Device Library's MSC class alongside the existing CDC class on the same USBD handle (composite device). Provide an MSC storage interface whose SCSI callbacks read raw 512-byte sectors from the SD card via the existing `sd_read_sectors()`. Keep CDC for serial debug/DFU.

**Tech Stack:** STM32L452RCT6, STM32duino core, ST USB Device Library (Core + Class/MSC Middlewares), FatFs (for exFAT mounting on the host side), our SdCard.cpp raw block driver.

## Global Constraints

- Do not modify `FLASH_OPTR`/option bytes; the board must keep BOOT-button DFU entry working.
- Keep the working audio pipeline intact; the change is USB-only.
- MSC volume is READ-ONLY (write returns an error) — per design spec, safest policy.
- USB PID: use a distinct PID for the composite (e.g. `0x5741`) or keep `0x5740`; verify Windows enumerates.
- `USBD_MAX_SUPPORTED_CLASS` is 4 in the bundled stack — composite CDC+MSC fits.
- Expose the full card capacity read via SCSI READ CAPACITY(10); use sector size 512.

---

### Task 1: MSC Storage Interface (SCSI callbacks over raw SD sectors)

**Files:**
- Create: `firmware/src/UsbMscStorage.cpp`
- Create: `firmware/src/UsbMscStorage.h`
- Modify: `firmware/src/SdCard.h` (expose `sd_read_sectors()` if not already public)

**Interfaces:**
- Consumes: `sd_read_sectors(uint32_t lba, uint16_t count, uint8_t* dst)` and `sd_capacity_bytes()` from `SdCard.cpp` (check exact signatures).
- Produces: `USBD_StorageTypeDef USBD_MSC_Storage_fops` (the ST MSC `STORAGE_*` callbacks) with `BOT_MEDIA_READY`/`BOT_MEDIA_ERROR` returns.

- [ ] **Step 1: Inspect the existing SD driver API**

Read `firmware/src/SdCard.h` and `SdCard.cpp`. Confirm the names/signatures of the raw sector read function (used by FatFs `disk_read`) and the capacity getter. Record them here so later tasks use exact names.

- [ ] **Step 2: Write the storage header**

```c
#pragma once
#include "usbd_def.h"
#include "usbd_msc.h"
extern USBD_StorageTypeDef USBD_MSC_Storage_fops;
```

- [ ] **Step 3: Implement the storage callbacks**

Implement `USBD_STORAGE_Init`, `USBD_STORAGE_GetCapacity` (returns `(uint32_t*)(uint16_t*)(uint16_t*)` block size 512 and block count = capacity/512), `USBD_STORAGE_IsReady`, `USBD_STORAGE_IsWriteProtected` (return 1), `USBD_STORAGE_Read` (call `sd_read_sectors`, return `USBD_OK`/`USBD_FAIL` mapped to `BOT_MEDIA_ERROR`), and `USBD_STORAGE_Write` (return `BOT_MEDIA_WRITE_PROTECTED`).

Use the template at `system/Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc/usbd_msc_storage_template.h` for the exact callback signatures.

- [ ] **Step 4: Compile the project**

Run `pio run -e dayvault` — the storage file must compile (it links only after USB integration, so wrap calls defensively).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/UsbMscStorage.*
git commit -m "feat(usb): MSC storage interface over raw SD sectors"
```

---

### Task 2: Composite CDC+MSC Configuration Descriptor

**Files:**
- Create: `firmware/src/UsbCompositeDesc.cpp` (device + config + string descriptors for a CDC+MSC composite)
- Modify: `firmware/src/usbd_desc.c` if overriding the core descriptor (or provide a custom `USBD_Desc`)

**Interfaces:**
- Consumes: the ST `USBD_DescriptorsTypeDef` shape (`USBD_DeviceDescriptor`, `USBD_LangIDStrDescriptor`, etc.)
- Produces: a `USBD_DescriptorsTypeDef USBD_Desc` whose `USBD_GetCfgDesc` returns a config descriptor containing BOTH the CDC interface (2 interfaces: control + data) and the MSC interface (1 bulk-in/1 bulk-out endpoint), each with correct endpoint addresses and `bInterfaceNumber`s.

- [ ] **Step 1: Study the HID-composite descriptor in the core**

Read `libraries/USBDevice/src/usbd_desc.c` (the `USBD_USE_HID_COMPOSITE` path) and the MSC descriptor layout in `system/Middlewares/.../Class/MSC/Inc/usbd_msc.h`. Understand how the core builds a multi-interface config descriptor.

- [ ] **Step 2: Write the composite config descriptor**

Write the CDC+MSC config descriptor. CDC uses interfaces 0/1 with endpoints (2, 3); MSC uses interface 2 with endpoints (6 IN, 7 OUT). Keep the CDC endpoints used by the current stack to avoid touching `usbd_ep_conf.c` more than needed. Verify total descriptor length against `USBD_MAX_NUM_INTERFACES` (bump to 3+ in `usbd_def.h` via build flag if needed).

- [ ] **Step 3: Compile and iterate**

Build; fix descriptor byte-length/interface-index issues until it compiles.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(usb): composite CDC+MSC configuration descriptor"
```

---

### Task 3: Register MSC Class and Boot as Composite

**Files:**
- Modify: `firmware/src/main.cpp` (or a new `UsbInit.cpp`) — replace/augment `CDC_init()` with a composite init that registers both classes.
- Modify: `platformio.ini` — add `-DUSBD_MAX_SUPPORTED_CLASS=4` if not already the default, and a PID/VID build define.

**Interfaces:**
- Consumes: `USBD_MSC_Storage_fops` (Task 1), `USBD_Desc` (Task 2).
- Produces: a working composite device that enumerates as serial + disk.

- [ ] **Step 1: Add the composite init function**

```c
void usb_composite_init(void) {
  if (USBD_Init(&hUSBD_Device, &USBD_Desc, 0) == USBD_OK) {
    USBD_RegisterClass(&hUSBD_Device, USBD_CDC_CLASS);
    USBD_CDC_RegisterInterface(&hUSBD_Device, &USBD_CDC_fops);
    USBD_RegisterClass(&hUSBD_Device, USBD_MSC_CLASS);
    USBD_MSC_RegisterInterface(&hUSBD_Device, &USBD_MSC_Storage_fops);
    USBD_Start(&hUSBD_Device);
  }
}
```

Wire it so it runs at boot (replace the Arduino CDC init path; disable `PIO_FRAMEWORK_ARDUINO_ENABLE_CDC` if it fights the composite, or call `USBDevice.detach()`/`CDC_deInit()` first).

- [ ] **Step 2: Build, flash, enumerate on Windows**

Flash and verify: Device Manager shows a CDC serial port AND a disk. Windows may need the card mounted (exFAT). Confirm the WAV files are browsable and copyable.

- [ ] **Step 3: Verify recording still works (USB detach path)**

Detach USB → confirm auto-recording starts (REC###.WAV written). Re-attach → disk enumerates with the new file.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(usb): composite CDC+MSC device enumerates as disk"
```

---

### Task 4: Hardware Verification & Regression

**Files:** none (verification only)

**Interfaces:** uses the composite device from Task 3.

- [ ] **Step 1: Verify disk read-only + file integrity**

Copy REC###.WAV from the disk to the PC; byte-compare with a serial DOWNLOAD of the same file (or `CHECK` command) — sizes must match.

- [ ] **Step 2: Verify no USB regressions**

CDC serial console still answers `INFO`/`REC`/`STOP`/`DFU`; BOOT-button DFU flash still works; option bytes untouched.

- [ ] **Step 3: Document**

Update `Docs/README.md` with the new export flow (plug in → copy files).

- [ ] **Step 4: Commit**

```bash
git commit -am "docs: USB MSC export flow"
```
