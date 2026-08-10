# DayVault High-Speed Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raise DayVault's reliable large-file export speed from the measured 240 KiB/s toward the 800 KiB/s to 1.0 MB/s practical range of USB Full Speed.

**Architecture:** Correct the STM32L452 memory map first, then optimize and measure each stage independently. Use SRAM2 ping-pong buffers between a multi-block SD reader and a continuously queued USB transport, with a resumable CRC-verified host protocol.

**Tech Stack:** STM32L452RCT6, Arduino STM32 core 2.12.0, STM32 HAL, FatFs, USB FS CDC/WinUSB, Python 3.14, pySerial, pytest, PySide6.

## Global Constraints

- Never modify option bytes or write `0x1FFF7800`.
- Preserve software DFU jump to `0x1FFF0000`, physical BOOT0 behavior, and USB VID `0x0483`.
- Verify `dfu-util -l` immediately before any flash and verify DFU again after normal-boot testing.
- Keep legacy commands working while the PC application migrates.
- Never delete a `.part` file merely because a transient transfer attempt failed.
- All throughput claims require fresh measured evidence.

---

### Task 1: Memory map and transfer workspace

**Files:**
- Modify: `firmware/boards/stm32l452rc.ld`
- Modify: `firmware/boards/dayvault_l452rc.json`
- Create: `firmware/src/TransferBuffer.h`
- Create: `firmware/src/TransferBuffer.cpp`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Produces `transfer_buffer(size_t index) -> uint8_t*` and
  `transfer_buffer_size() -> size_t`, backed by two 16 KiB buffers in `.ram2`.

- [ ] Add a map-file regression script/test that expects 128 KiB SRAM1, 32 KiB SRAM2, and two `.ram2` buffers.
- [ ] Run it against the current build and confirm it fails because SRAM1 is reported as 64 KiB and `.ram2` is absent.
- [ ] Correct the linker and board memory sizes; add the `.ram2` section and transfer-buffer module.
- [ ] Replace `download_file2` and `SDSPEED` 16 KiB stack arrays with the shared transfer workspace.
- [ ] Build and run the map assertion; confirm adequate SRAM1 stack headroom.

### Task 2: Benchmark protocol and host parser

**Files:**
- Create: `tools/dayvault_sync/dayvault/benchmark.py`
- Create: `tools/dayvault_sync/tests/test_benchmark.py`
- Modify: `firmware/src/main.cpp`
- Modify: `Docs/Serial-Command-Reference.md`

**Interfaces:**
- Produces `parse_benchmark_line(line: str) -> BenchmarkResult`.
- Firmware outputs `BENCH <layer> bytes=<n> ms=<n> kib_s=<n> crc32=<hex>`.

- [ ] Write parser tests for valid, malformed, and zero-time results and verify they fail.
- [ ] Implement the parser and make tests pass.
- [ ] Add deterministic USB-only, SD-only, and end-to-end firmware benchmark output.
- [ ] Build firmware and verify all host tests.

### Task 3: Buffered SD payload I/O

**Files:**
- Modify: `firmware/src/SdCard.cpp`
- Modify: `firmware/src/SdCard.h`
- Create: `firmware/test/test_sd_protocol/test_sd_protocol.cpp`

**Interfaces:**
- Keeps `sd_read_sectors(uint32_t, uint8_t*, uint32_t)` unchanged.
- Adds diagnostic counters returned by `sd_get_stats(sd_stats_t*)`.

- [ ] Add host-native tests for command/address calculation and CMD18/CMD12 state transitions using an injected SPI fake.
- [ ] Verify the tests fail with the current CMD17-only implementation.
- [ ] Add one-call payload receive and CMD18 multi-block reads with CMD17 fallback.
- [ ] Set verified normal speed to 20 MHz and retain initialization below 400 kHz.
- [ ] Run native tests and build the target firmware.

### Task 4: Resumable CRC-verified export protocol

**Files:**
- Create: `firmware/src/ExportProtocol.h`
- Create: `firmware/src/ExportProtocol.cpp`
- Create: `tools/dayvault_sync/dayvault/export_protocol.py`
- Create: `tools/dayvault_sync/tests/test_export_protocol.py`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Command: `GET2 <offset> <filename>`.
- Header: `GET2START size=<total> offset=<offset> length=<remaining>\n`.
- Trailer: `GET2END sent=<n> crc32=<8-hex-digits>\n`.

- [ ] Write host tests for header/trailer parsing, bad offsets, and CRC mismatch; verify failure.
- [ ] Implement pure parsers and CRC helpers; make tests pass.
- [ ] Implement the firmware command using the SRAM2 transfer buffers while retaining `DL2`.
- [ ] Build firmware and test deterministic CRC vectors.

### Task 5: PC resumable transfer pipeline

**Files:**
- Modify: `tools/dayvault_sync/dayvault/dio.py`
- Modify: `tools/dayvault_sync/dayvault/engine.py`
- Modify: `tools/dayvault_sync/dayvault/app.py`
- Modify: `tools/dayvault_sync/tests/test_dio.py`
- Modify: `tools/dayvault_sync/tests/test_engine.py`

**Interfaces:**
- Produces `download_get2(name, part_path, expected_size, progress_cb, interrupt) -> int`.

- [ ] Write fake-serial tests proving partial-file resume, CRC validation, interrupt preservation, and retry without truncation.
- [ ] Verify every new test fails against the existing DL2 downloader.
- [ ] Implement reusable read buffers, buffered disk output, and throttled progress callbacks.
- [ ] Prefer GET2 and fall back to DL2 only for older firmware.
- [ ] Run the complete host suite.

### Task 6: USB FS transmit pipeline

**Files:**
- Create or modify repo-local USB transport files selected by `firmware/platformio.ini`.
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/platformio.ini`

**Interfaces:**
- Provides a blocking producer API with internal asynchronous packet queuing;
  completion interrupts release buffers to the SD reader.

- [ ] Capture a fresh USB-only benchmark before changing the transport.
- [ ] Add compile/build checks for endpoint addresses and total 1024-byte PMA use.
- [ ] Enable continuous IN transfers and double buffering without changing DFU behavior.
- [ ] Measure again; retain only changes that improve throughput and pass integrity checks.
- [ ] If CDC stays below 700 KiB/s, add a vendor-specific WinUSB bulk interface and Microsoft OS descriptors, plus a Python WinUSB backend with multiple pending reads.

### Task 7: Physical validation and documentation

**Files:**
- Modify: `Docs/Serial-Command-Reference.md`
- Create: `Docs/High-Speed-Transfer.md`
- Modify: `tools/dayvault_sync/README.md`

- [ ] Run host tests and a clean firmware build.
- [ ] Confirm no source or script writes option bytes or `0x1FFF7800`.
- [ ] Confirm `dfu-util -l`, flash once, test normal boot, then reconfirm DFU.
- [ ] Measure USB-only, SD-only, and end-to-end throughput with CRC verification.
- [ ] Record rates, test setup, remaining bottleneck, and full-day export estimate.
