# High-Speed Audio Export

DayVault exports recordings through a dedicated, resumable WinUSB bulk protocol.
The implementation optimizes the complete path rather than only increasing a
single buffer size:

- FatFs uses SD `CMD18` multi-block reads with `CMD12` termination.
- Two 16 KiB buffers live in STM32L452 SRAM2, outside the task stack.
- USB CDC uses a larger bounded TX queue and continuous writes for compatibility.
- `BULK2` temporarily switches from CDC PID `0x5740` to WinUSB PID `0x5741`.
- Every transfer carries its exact byte count and IEEE CRC32.
- `.part` files resume from their current length after an interruption.
- The host writes in 256 KiB chunks and never loads a complete recording into RAM.

## Protocol

```text
CDC host -> BULK2 <offset> <filename>\r\n
CDC dev  -> BULK2READY size=<total> offset=<offset> length=<remaining>\r\n

Device re-enumerates as 0483:5741 (WinUSB)

Bulk IN  -> GET2START size=<total> offset=<offset> length=<remaining>\r\n
Bulk IN  -> <exactly remaining binary bytes>
Bulk IN  -> GET2END sent=<remaining> crc32=<8 hex digits>\r\n
Bulk IN  -> BENCH bulk bytes=<remaining> ms=<n> kib_s=<n> crc32=<crc>\r\n
Bulk OUT <- DONE\n

Device returns to CDC 0483:5740
```

The desktop sync order is `BULK2` -> `GET2` -> `DL2`, allowing new firmware to
use the fastest verified path without breaking older boards.

## Measured Results

Prototype measurements from 2026-08-11:

| Layer | Before | Optimized | Verification |
|---|---:|---:|---|
| SD read | about 390 KiB/s | about 671 KiB/s | multi-block command counters + CRC32 |
| CDC end-to-end | about 67 KiB/s | about 235 KiB/s | byte count + CRC32 |
| WinUSB bulk | not available | 225-227 KiB/s | three consecutive 2 MiB CRC matches |

The real-file test exported `4,508,018` bytes from microSD. The destination was
then truncated to `1,000,000` bytes and resumed; both complete copies produced
SHA-256 `3e63cc51bd2f5fd9752c3d6ace1e5f91c811744b1ee7cbe4d10d83faae52ba7b`.

## USB Full-Speed Limit

The `12` in USB Full-Speed means **12 Mbit/s**, not 12 MiB/s. The raw line
rate is therefore only 1.5 MB/s, or about 1.43 MiB/s. USB packet headers,
tokens, handshakes, inter-packet timing, and frame scheduling reduce the ideal
bulk-payload ceiling to roughly 1.16 MiB/s before firmware and storage overhead.

The current 225-227 KiB/s result is a verified implementation result, not the
physical Full-Speed ceiling. More low-level host and device work may improve it,
but this STM32L452 USB peripheral can never deliver 12 MiB/s. Reaching that rate
requires different hardware with USB High-Speed or another interface capable of
more than 100 Mbit/s.

## Reliability Choice

The STM32L4 HAL double-PMA-buffer transmit path was evaluated on hardware. It
reported completion while bytes were reordered or overwritten at large-transfer
boundaries, and its measured throughput remained around 233-235 KiB/s. DayVault
therefore uses single-PMA-buffer bulk IN, which completed every CRC test and has
essentially the same measured speed.

## Benchmark Commands

```powershell
python tools/benchmark_usb.py --port COM9
python tools/benchmark_winusb.py --port COM9 --timeout 45
```

Firmware build:

```powershell
pio run -d firmware -e dayvault
```

Never write STM32 option bytes while testing transfer firmware. DayVault DFU
updates target only application flash alt 0 at `0x08000000`.
