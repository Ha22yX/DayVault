# DayVault High-Speed Export - Design

Date: 2026-08-10
Status: Approved by user

## Goal

Reduce the time required to export a full day of recordings over the existing
USB-C connector while preserving reliable recording, resumable downloads, and
the board's recoverable DFU path.

The current measured USB-only rate is about 240 KiB/s. The practical target for
the existing STM32L452 USB Full Speed hardware is 800 KiB/s or better for a
large contiguous file, with correctness and safe recovery taking priority over
a benchmark-only peak.

## Non-negotiable safety constraints

- Never modify STM32 option bytes, especially `nBOOT1`, `nBOOT0`, or
  `nSWBOOT0`.
- Never write the option-byte address range at `0x1FFF7800`.
- Preserve the existing software DFU reset flag and jump to system memory at
  `0x1FFF0000`.
- Keep ST's USB VID `0x0483`.
- Before any physical flash, verify DFU with `dfu-util -l`; after flashing,
  verify normal boot and that DFU remains reachable.
- A failed or interrupted download must never damage the recording on the SD
  card.

## Current bottlenecks

1. The linker exposes only 64 KiB of the STM32L452's 128 KiB SRAM1 and leaves
   the separate 32 KiB SRAM2 unused. Large 16 KiB local arrays overlap the
   approximately 5.5 KiB remaining stack.
2. SD reads use CMD17 once per 512-byte sector and call
   `HAL_SPI_TransmitReceive` once per byte at 10 MHz.
3. USB CDC uses one active IN transfer and a 2 KiB queue. The measured USB-only
   path reaches about 240 KiB/s, far below the Full Speed bulk ceiling.
4. `DL2` has no offset resume, end-to-end checksum, or machine-readable final
   status. The PC deletes partial files after a retry.
5. The PC performs blocking reads and writes each received chunk directly to
   disk, with progress callbacks on every chunk.

## Architecture

### 1. Correct memory map and fixed transfer workspace

- Expose 128 KiB SRAM1 at `0x20000000` and 32 KiB SRAM2 at `0x10000000`.
- Add an explicit `.ram2` `NOLOAD` section.
- Allocate two aligned 16 KiB transfer buffers in SRAM2. No large transfer
  buffers may live on the stack.
- Keep ordinary globals, heap, and stack in SRAM1. Report SRAM1 and SRAM2 usage
  independently in the map file.

### 2. Layered performance measurements

Provide independent commands and parsers for:

- USB generator to host: no SD access.
- SD file read to memory: no USB payload.
- End-to-end file export: SD through USB to PC disk.

Every result includes bytes, elapsed milliseconds, and integer KiB/s. The host
test also validates a deterministic byte pattern or a final CRC32.

### 3. SD read path

- Keep command traffic byte-oriented, but receive payload blocks with one HAL
  buffer transaction instead of 512 one-byte calls.
- Use CMD18 for contiguous multi-sector reads and CMD12 to stop, with CMD17 as
  a compatibility fallback.
- Raise normal SPI clock from 10 MHz to 20 MHz first. A 40 MHz/high-speed mode
  is enabled only after card capability negotiation and repeated integrity
  tests.
- Feed the watchdog while waiting for tokens and during long transfers.
- Preserve the existing write path until the read optimization is verified.

### 4. Export protocol

Introduce a versioned binary export command while keeping legacy `DL2` during
migration. The new command accepts a filename and byte offset, aligns SD access
to sectors internally, streams from that offset, and finishes with a structured
status containing total size, bytes sent, and CRC32 for the transmitted range.

The PC keeps `<name>.part`, resumes from its current length, and atomically
renames it only after size and CRC verification. Protocol parsing is isolated
from serial I/O so it can be unit-tested without hardware.

### 5. USB transport

Optimization is staged so every step is measurable:

1. Remove avoidable CDC copies and enlarge producer blocks after memory safety
   is fixed.
2. Configure the USB FS bulk IN path for continuous queued transfers and, where
   supported by the STM32 USB device library, double-buffer its PMA endpoint.
3. If Windows CDC remains the limiting layer, add a vendor-specific WinUSB bulk
   interface with Microsoft OS descriptors. The command/control interface may
   remain CDC during migration.

The final transport must keep USB packets continuously queued; SD reads and USB
transmits alternate between the two SRAM2 buffers.

### 6. PC pipeline

- Read into reusable large buffers and batch disk writes.
- Throttle GUI progress events by time and percentage rather than per read.
- Preserve partial files on transient errors.
- Reconnect and resume automatically from the verified partial length.
- Use several asynchronous WinUSB reads when that backend is enabled.

## Verification

- Host protocol, resume, CRC, and failure handling have automated tests.
- Firmware builds with map-file assertions proving SRAM1 stack headroom and
  exact SRAM2 buffer placement.
- SD integrity is checked by repeated reads and CRC comparison before raising
  its clock.
- Physical benchmarks record USB-only, SD-only, and end-to-end rates using the
  same board and file.
- A final DFU recovery check is mandatory; no option-byte command appears in
  source, scripts, or logs.

## Expected result

For a 3.64 GB day, 800 KiB/s reduces export to roughly 74 minutes; 1.0 MB/s is
roughly 61 minutes. The exact result depends on the SD card and Windows host,
but the design removes the current software bottlenecks without requiring a
PCB revision.
