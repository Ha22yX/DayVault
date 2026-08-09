# Battery Monitoring + Real-Time Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add battery voltage monitoring with low-battery protection (device enters STOP sleep when the 400 mAh LiPo is low) and a real-time clock that is set over the serial port and keeps time across sleep modes.

**Architecture:** Two new modules: `Battery` (ADC1 on PA0, 1 M/1 M divider) and `DeviceTime` (RTC calendar driven by the 32.768 kHz LSE crystal on PC14/PC15). `main.cpp` wires a `SETTIME <unix>` command, reports time + battery in `INFO`, and enters STOP mode (RTC alarm every 4 s + PA9 EXTI for USB attach) when the battery is critically low, re-checking on every wake.

**Tech Stack:** STM32L452RCT6, STM32duino core, HAL ADC/RTC/PWR modules, the existing serial command loop.

## Global Constraints

- Never modify FLASH option bytes / boot config; BOOT-button and software `DFU` entry must keep working.
- Do not break the working audio pipeline (PDM capture, SD recording, `DL2` download).
- `dbg_iwdg_kick()` must remain in `loop()` — removing it causes a 5 s reset loop (documented in `Docs/Serial-Command-Reference.md`).
- The RTC calendar keeps time in the backup domain; it must keep running across STOP sleep.
- The device is battery powered (400 mAh LiPo → `TPS63031` → `3V3`). VBAT is tied to `3V3` (not raw battery), so the RTC stays powered as long as the 3.3 V rail is up.

---

### Task 1: DeviceTime — LSE + RTC init, time get/set

**Files:**
- Create: `firmware/src/DeviceTime.h`
- Create: `firmware/src/DeviceTime.cpp`
- Modify: `firmware/platformio.ini` (add `-DHAL_RTC_MODULE_ENABLED`)

**Interfaces:**
- Produces: `void dt_init(void)`, `void dt_set_unix(uint32_t unix)`, `uint32_t dt_get_unix(void)`, `void dt_format(char* buf, size_t len)` (writes `YYYY-MM-DD HH:MM:SS`).

- [ ] **Step 1: Enable the RTC HAL module**

Add `-DHAL_RTC_MODULE_ENABLED` to `build_flags` in `firmware/platformio.ini`.

- [ ] **Step 2: Write the header**

```c
#pragma once
#include <stdint.h>
#include <stddef.h>
void dt_init(void);
void dt_set_unix(uint32_t unix);
uint32_t dt_get_unix(void);
void dt_format(char* buf, size_t len);
```

- [ ] **Step 3: Implement LSE + RTC init and unix conversions**

`dt_init()`: enable PWR/backup access (`__HAL_RCC_PWR_CLK_ENABLE(); HAL_PWR_EnableBkUpAccess();`), enable LSE and wait `LSERDY`, select LSE as RTC clock (`RCC_BackupResetCmd`/`RCC_RTCCLKConfig` per the HAL `HAL_RCCEx_PeriphCLKConfig` with `RCC_PERIPHCLK_RTC` and `RCC_RTCAPB_CLKSOURCE_LSE`), init the RTC handle (`HAL_RTC_Init` with `RTC_HOURFORMAT_24`), and if the RTC has no valid time, default to 2026-01-01 00:00:00.

`dt_set_unix(uint32_t unix)`: convert unix seconds to civil date/time (implement a small Gregorian algorithm, e.g. the standard days-to-civil algorithm; no external library), write with `HAL_RTC_SetTime` / `HAL_RTC_SetDate`.

`dt_get_unix()`: read with `HAL_RTC_GetTime` / `HAL_RTC_GetDate` and convert civil → unix (inverse algorithm).

`dt_format(buf, len)`: format the current RTC time as `YYYY-MM-DD HH:MM:SS`.

Do **not** use `time.h`/`mktime` (newlib may pull unexpected memory). Use the integer days-from-civil / civil-from-days algorithms (public domain) with `uint32_t`.

- [ ] **Step 4: Build**

Run `pio run -e dayvault`. Must compile cleanly. Do not flash yet.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/DeviceTime.* firmware/platformio.ini
git commit -m "feat(rtc): DeviceTime - LSE RTC init with unix get/set and formatting"
```

---

### Task 2: Battery — ADC1 on PA0

**Files:**
- Create: `firmware/src/Battery.h`
- Create: `firmware/src/Battery.cpp`
- Modify: `firmware/platformio.ini` (add `-DHAL_ADC_MODULE_ENABLED`)

**Interfaces:**
- Consumes: none.
- Produces: `void bat_init(void)`, `uint16_t bat_millivolts(void)`, `uint8_t bat_percent(void)`.

- [ ] **Step 1: Enable the ADC HAL module**

Add `-DHAL_ADC_MODULE_ENABLED` to `build_flags`.

- [ ] **Step 2: Write the header**

```c
#pragma once
#include <stdint.h>
void bat_init(void);
uint16_t bat_millivolts(void);   /* e.g. 3650 = 3.65 V at the battery terminal */
uint8_t  bat_percent(void);      /* 0..100, linear 3.0 V..4.2 V */
```

- [ ] **Step 3: Implement the ADC read with VREFINT calibration and averaging**

`bat_init()`: enable ADC1 + GPIOA clocks, configure PA0 as `GPIO_MODE_ANALOG` with `GPIO_PULLDOWN`, `HAL_ADC_Init` (12-bit, `ADC_SAMPLINGTIME_247CYCLES_5` or longer per the pinout note "long ADC sample time"), then `HAL_ADCEx_Calibration_Start`.

**Measurement accuracy** (this matters for threshold decisions — do not skip):

- Calibrate VDDA using the internal reference: read the VREFINT channel, then
  `vdda_mv = (uint32_t)3300u * (uint32_t)VREFINT_CAL / (uint32_t)vrefint_code` where
  `VREFINT_CAL` is the factory calibration value at `0x1FFF75AA` (16-bit read, `*(const uint16_t*)0x1FFF75AAu`) and `vrefint_code` is the raw ADC code of the VREFINT channel. This corrects for real VDDA drift.
- Compute the battery reading as `bat_mv = (uint32_t)code * vdda_mv * 2u / 4096u` (the ×2 is the 1 M/1 M divider).
- **Average 8 consecutive reads** (a tight loop of `HAL_ADC_Start`/`PollForConversion`/`Stop` for one PA0 + one VREFINT read each) and return the mean, to suppress noise. Do this inside `bat_millivolts()`.

`bat_percent()`: linear map 3000 mV → 0 %, 4200 mV → 100 %, clamped.

- [ ] **Step 4: Build**

`pio run -e dayvault` must compile cleanly.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/Battery.* firmware/platformio.ini
git commit -m "feat(battery): Battery - ADC1 PA0 read with mV/percent"
```

---

### Task 3: Serial commands — SETTIME, and INFO time/battery

**Files:**
- Modify: `firmware/src/main.cpp` (command parser, `INFO` handler)
- Modify: `firmware/src/main.cpp` (`setup()` calls `dt_init()` + `bat_init()`)

**Interfaces:**
- Consumes: `dt_init()`, `dt_set_unix()`, `dt_format()`, `bat_init()`, `bat_millivolts()`, `bat_percent()`.
- Produces: new commands `SETTIME <unix>`; extended `INFO` output.

- [ ] **Step 1: Call init in setup()**

In `setup()` (after `dbg_iwdg_init()`), add:

```c
dt_init();
bat_init();
```

Include `"DeviceTime.h"` and `"Battery.h"`.

- [ ] **Step 2: Add the SETTIME command**

In the command parser (`loop()`), before the `else` fallback:

```c
} else if (strncmp(line, "SETTIME ", 8) == 0) {
    uint32_t unix = (uint32_t)strtoul(line + 8, NULL, 10);
    dt_set_unix(unix);
    char tb[32];
    dt_format(tb, sizeof(tb));
    Serial.print("TIME set to "); Serial.println(tb);
}
```

- [ ] **Step 3: Extend INFO**

In the `INFO` handler, after the existing fields, add:

```c
char tb[32];
dt_format(tb, sizeof(tb));
Serial.print(" time="); Serial.print(tb);
Serial.print(" bat="); Serial.print(bat_millivolts()); Serial.print("mV");
Serial.print(" pct="); Serial.print(bat_percent());
```

- [ ] **Step 4: Build, flash, verify**

Build + flash via software DFU. Verify: `INFO` prints `time=... bat=...mV pct=...`; `SETTIME 1700000000` then `INFO` shows the matching date and time advancing.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat: SETTIME command + INFO reports time and battery"
```

---

### Task 4: Low-battery STOP sleep with periodic re-check + USB wake

**Files:**
- Modify: `firmware/src/main.cpp` (loop + new helpers)
- Modify: `firmware/src/DeviceTime.h/.cpp` (add `dt_next_alarm_in(uint16_t sec)` / wake-alarm support)

**Interfaces:**
- Consumes: `bat_millivolts()`, `bat_percent()`, `dt_init()`.
- Produces: `static void low_battery_sleep(void)` in main.cpp; behavior documented below.

- [ ] **Step 1: Add an RTC wake-alarm helper to DeviceTime**

Add to `DeviceTime.h`:

```c
void dt_set_wake_alarm(uint16_t seconds_from_now);
```

Implement in `DeviceTime.cpp` using `HAL_RTC_SetAlarm_IT` with an alarm 4 s in the future (read current time, add 4 s, set the alarm), and keep the RTC alarm interrupt enabled (`HAL_RTCEx_SetWakeUpTimer_IT` is an alternative — use the RTC wake-up timer if the HAL on this core exposes `HAL_RTCEx_SetWakeUpTimer_IT`; either is acceptable, document which).

- [ ] **Step 2: Configure the wake sources before sleeping**

In `main.cpp`, a helper that, immediately before entering STOP:

```c
static void low_battery_enter_stop(void)
{
    /* wake on PA9 (USB detect) rising edge: USB attach -> charge */
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    /* EXTI line for PA9 configured once in setup (falling/rising both acceptable; wake on any edge) */
    dt_set_wake_alarm(4);
    dbg_iwdg_kick();                 /* refresh before sleep; wakeup every 4 s refreshes again */
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    /* --- resumed --- */
    SystemClock_Config();            /* re-init clocks after STOP (RCC falls back to MSI) */
    HAL_ResumeTick();
}
```

- [ ] **Step 3: Wire PA9 EXTI in setup()**

In `setup()`, configure PA9 (`PIN_USB_DETECT`) as an EXTI source (`HAL_GPIO_EXTI_ConfigLine` or raw `SYSCFG->EXTICR` + `EXTI->IMR`/`RTSR`). Add an empty `HAL_GPIO_EXTI_Callback` in main.cpp (or a weak handler) so the wake interrupt is consumed.

- [ ] **Step 4: Drive sleep from loop() with HYSTERESIS (no threshold flapping)**

Use **latching state with hysteresis**: enter sleep only when the voltage stays below the sleep threshold for a sustained period; once asleep, wake/resume only when the voltage rises above a strictly higher resume threshold (e.g., USB charging). This prevents the device from flipping between sleep and run when the voltage hovers near a single threshold. Also gate the ADC read to at most ~1 Hz so the threshold checks are stable.

```c
/* thresholds (mV): sleep below 3000, resume above 3300 — do NOT use one threshold */
#define BAT_SLEEP_MV     3000u
#define BAT_RESUME_MV    3300u

static uint8_t bat_asleep = 0;
static uint32_t low_start = 0;
static uint32_t last_bat_ms = 0;

if ((millis() - last_bat_ms) >= 1000) {
    last_bat_ms = millis();
    uint16_t mv = bat_millivolts();
    if (bat_asleep) {
        if (mv >= BAT_RESUME_MV) bat_asleep = 0;   /* charged enough (e.g. USB) -> resume */
    } else if (mv < BAT_SLEEP_MV) {
        if (low_start == 0) low_start = millis();
        if ((millis() - low_start) > 3000) {        /* sustained low -> sleep */
            low_battery_enter_stop();               /* returns on wake; re-checks below */
            bat_asleep = 1;                         /* latched: stay asleep until > RESUME */
            low_start = 0;
        }
    } else {
        low_start = 0;
    }
}
```

Note: when `bat_asleep` is latched, the device still wakes every 4 s (RTC alarm) to refresh the IWDG and re-check the battery; it re-enters STOP immediately if `mv < BAT_RESUME_MV`. It only stays awake when charging has lifted the voltage above `BAT_RESUME_MV`.

- [ ] **Step 4b: Debounce the recording start/stop (USB detect)**

The auto recording toggles on `PIN_USB_DETECT` (PA9) edge in `loop()`. To avoid flapping when the pin is noisy or near a threshold, change the raw edge detection to a debounced one (stable for ~100 ms before acting). Current code at `main.cpp` around lines 733-742 is:

```c
int usb = digitalRead(PIN_USB_DETECT);
if (last_usb < 0) { last_usb = usb; if (usb == LOW) rec_start(); }
if (usb != last_usb) { last_usb = usb; if (usb == LOW) rec_start(); else rec_stop(); }
```

Replace with a state that requires the new level to persist for `USB_DEBOUNCE_MS` (100) before calling `rec_start()` / `rec_stop()`:

```c
int usb = digitalRead(PIN_USB_DETECT);
if (usb != last_usb) {
    usb_pending = usb;
    usb_pending_since = millis();
    last_usb = usb;
}
if (usb_pending >= 0 && (millis() - usb_pending_since) >= 100) {
    if (usb_pending == 0) rec_start(); else rec_stop();
    usb_pending = -1;
}
```

with `static int usb_pending = -1; static uint32_t usb_pending_since = 0;` declared in `loop()`. This keeps the battery sleep path and the recording path independent and flapping-free.

- [ ] **Step 5: IWDG during sleep**

Because the IWDG cannot be disabled once started, the design wakes every 4 s (RTC alarm) to call `dbg_iwdg_kick()` and re-sleep. The STOP → wake → re-sleep cycle must complete in under the 5 s IWDG window. Verify by measuring that the device stays alive (no reset) while in low-battery sleep for > 30 s.

- [ ] **Step 6: Build, flash, verify on hardware**

Set `SETTIME`, confirm time advances through sleep. With a low battery (or by temporarily lowering the threshold to ~3300 mV for the test), confirm the device enters STOP (current drops, serial stops responding) and wakes within ~4 s, still responsive, with the clock correct. Confirm USB attach wakes it.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/DeviceTime.* firmware/src/main.cpp
git commit -m "feat(power): low-battery STOP sleep with 4s RTC re-check and USB wake"
```

---

### Task 5: Documentation

**Files:**
- Modify: `Docs/Serial-Command-Reference.md`

- [ ] **Step 1: Document the new commands**

Add `SETTIME <unix>` to the command table, and note that `INFO` now reports `time=YYYY-MM-DD HH:MM:SS`, `bat=..mV`, `pct=..`.

- [ ] **Step 2: Document battery/sleep behavior**

Add a section: low-battery thresholds (sleep at 3.0 V, warn at 3.4 V), STOP sleep with 4 s RTC wake, USB-attach wake, and the IWDG/4 s-wake interaction.

- [ ] **Step 3: Commit**

```bash
git add Docs/Serial-Command-Reference.md
git commit -m "docs: SETTIME, INFO time/battery, low-battery sleep"
```
