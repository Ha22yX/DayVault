# USB DFU Auto-Entry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first firmware milestone for DayVault: a PlatformIO/STM32CubeL4 application that enumerates as a USB CDC device and, on a `DFU\n` command over the serial line, safely stops recording hooks and software-jumps into the STM32L452 system-memory USB DFU bootloader at `0x1FFF0000`.

**Architecture:** Bare-metal superloop with no RTOS. Pure-logic layer (`usbproto`, `dfu` command parse) is host-tested via a `native` PlatformIO env with Unity; hardware layers (`hw_usb`, `dfu_enter`, `main`) are compile-only until board bring-up. ST USB Device Library (CDC class) is auto-built by the `stm32cube` framework; the project supplies `usbd_conf.h`, `usbd_desc.c` and CDC glue.

**Tech Stack:** PlatformIO Core 6.1.19, ststm32 platform (installed), stm32cube framework (STM32CubeL4, installed at `~/.platformio/packages/framework-stm32cubel4`), native platform + Unity for host tests, C11 (host) / C99 (device, `-Os`).

**Design spec:** `Docs/09-USB-DFU-Entry-Design.md`

## Global Constraints

- Build command (device): `pio run -e dayvault`; tests: `pio test -e native`.
- MCU: STM32L452RCT6, 64-pin LQFP, **256 KB flash / 64 KB RAM** (256KB flash = `maximum_size 262144`, 64KB RAM = `maximum_ram_size 65536`). Custom board `boards/dayvault_l452rc.json` required — do NOT use `nucleo_l452re` (that is 512KB/160KB).
- Pin mapping (from Docs/02, MUST match): USB_DM=PA11, USB_DP=PA12, USB_DETECT=PA9, BAT_SENSE=PA0, PDM_CLK=PC2, PDM_DATA=PB12, SD_CS=PA4, SD_SCK=PA5, SD_MISO=PA6, SD_MOSI=PA7, LED=PA8, SWD=PA13/PA14.
- USB Full Speed device; USB kernel clock must be 48 MHz.
- ROM DFU entry: software jump to `0x1FFF0000` (STM32L452 system memory). No option-byte/flash writes — jumping back to the application after DFU is via normal reset.
- USB CDC protocol is line-based: a full line `DFU` (with optional `\r\n`) triggers DFU entry.
- Safe-stop hooks are injected callbacks; recording pipeline does not exist yet, so hooks are no-ops in this milestone.
- `use_usb` stack uses `USE_HAL_*_CALLBACKS=0` convention; polling + HAL overrides only where the framework requires.
- All blocking paths bounded; no unbounded waits.
- No comments in code unless they carry hardware/fact context (repo style).

---

## File Structure

```
firmware/
  platformio.ini                     Modify: add env:dayvault + env:native
  boards/dayvault_l452rc.json        Create: custom board (256KB flash / 64KB RAM)
  include/
    stm32l4xx_hal_conf.h             Create: HAL config (trimmed, USB PCD enabled)
    dayvault_config.h                Create: pins, USB clock, buffer sizes
    usbproto.h                       Create: line protocol parser
    usbd_conf.h                      Create: PCD + CDC activation config
    usbd_desc.h                      Create
    hw_usb.h                         Create: CDC glue + poll entry
    dfu.h                            Create: dfu_enter + hooks
    app.h                            Create: superloop entry
  src/
    main.c                           Create: clock tree (48 MHz USB), boot sequence
    app.c                            Create: superloop, dispatches CDC to dfu_enter
    usbproto.c                       Create: line parser (pure logic)
    hw_usb.c                         Create: USBD CDC glue, USBD_LL_* PCD glue
    usbd_desc.c                      Create: device descriptor
    usbd_cdc_if.c                    Create: CDC callbacks (rx line buffer)
    dfu.c                            Create: dfu_enter (compile-only)
  test/
    test_usbproto/test_usbproto.c    Create: Unity host tests for the parser
```

---

### Task 1: Scaffold PlatformIO project + custom board + native test env

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/boards/dayvault_l452rc.json`
- Create: `firmware/include/stm32l4xx_hal_conf.h`
- Create: `firmware/include/dayvault_config.h`
- Create: `firmware/include/usbproto.h`
- Create: `firmware/include/dfu.h`
- Create: `firmware/test/test_usbproto/test_usbproto.c`

**Interfaces:**
- Produces: `usbproto_t` with `usbproto_feed(usbproto_t*, uint8_t)`, `usbproto_poll(usbproto_t*)` returning a `usbproto_event_t` enum; `dfu_enter_with_hooks(const dfu_stop_hooks_t*)`. Consumed by Tasks 2-4.
- `dfu_stop_hooks_t { void (*stop_acquisition)(void); void (*close_segment)(void); void (*unmount_storage)(void); }`

- [ ] **Step 1: Create `firmware/boards/dayvault_l452rc.json`**

```json
{
  "build": {
    "core": "stm32",
    "cpu": "cortex-m4",
    "extra_flags": "-DSTM32L4 -DSTM32L452xx",
    "f_cpu": "80000000L",
    "mcu": "stm32l452rct6",
    "product_line": "STM32L452xx",
    "variant": "STM32L4xx/L452RC(I-T-Y)_L452RE(I-T-Y)x(P)_L462RE(I-T-Y)"
  },
  "debug": {
    "default_tools": ["stlink"],
    "jlink_device": "STM32L452RC",
    "openocd_board": "st_nucleo_l4",
    "openocd_target": "stm32l4x",
    "svd_path": "STM32L4x2.svd"
  },
  "frameworks": ["cmsis", "stm32cube"],
  "name": "DayVault L452RC",
  "upload": {
    "maximum_ram_size": 65536,
    "maximum_size": 262144,
    "protocol": "stlink",
    "protocols": ["stlink", "jlink", "cmsis-dap"]
  },
  "url": "",
  "vendor": "DayVault"
}
```

Note: `variant` string is copied verbatim from `nucleo_l452re.json` (the STM32L4xx CMSIS variant dir covers L452RC). `maximum_size 262144` and `maximum_ram_size 65536` are the L452RC correct values — do not inherit nucleo's 512KB/160KB.

- [ ] **Step 2: Create `firmware/platformio.ini`**

```ini
[platformio]
default_envs = dayvault

[env:dayvault]
platform = ststm32
board = dayvault_l452rc
framework = stm32cube
board_build.flash_size = 256KB
build_type = release
monitor_speed = 115200
build_flags =
    -Os
    -std=gnu99
    -DUSBD_ACTIVATE_CDC=1

[env:native]
platform = native
test_framework = unity
build_src_filter =
    -<*>
    +<src/usbproto.c>
test_build_src = true
```

Note: `USBD_ACTIVATE_CDC=1` selects the CDC device class during framework USB-lib build. The `-<*> +<src/usbproto.c>` filter keeps the native env compiling only the pure-logic module. `build_src_filter` also governs the `dayvault` env automatically; the `+<src/usbproto.c>` entry is harmless there because the dayvault env has no `build_src_filter` key yet — Task 3 adds `-<test/*>` so device builds exclude tests.

- [ ] **Step 3: Create `firmware/include/stm32l4xx_hal_conf.h`**

Start from the template in the framework package:

```bash
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Drivers\STM32L4xx_HAL_Driver\Inc\stm32l4xx_hal_conf_template.h" firmware\include\stm32l4xx_hal_conf.h
```

Then in the copied file, **enable only these modules** (comment out the rest), so PCD/USB builds and flash stays small:

```c
#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
```

Also set the assertion/clock config macros at the top to:

```c
#define HSE_VALUE       ((uint32_t)8000000U)
#define HSI_VALUE       ((uint32_t)16000000U)
#define LSE_VALUE       ((uint32_t)32768U)
#define LSI_VALUE       ((uint32_t)32000U)
#define MSI_VALUE       ((uint32_t)4000000U)
#define VDD_VALUE       ((uint32_t)3300U)
#define TICK_INT_PRIORITY 0x0FU
#define USE_RTOS        0U
#define PREFETCH_ENABLE 1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE 1U
```

Note: `USE_HAL_PCD_REGISTER_CALLBACKS` must be `0U` (polling + override style). Keep `USE_FULL_ASSERT` off for the release build.

- [ ] **Step 4: Create `firmware/include/dayvault_config.h`**

```c
#ifndef DAYVAULT_CONFIG_H
#define DAYVAULT_CONFIG_H

#include "stm32l4xx_hal.h"

#define PIN_USB_DETECT    GPIO_PIN_9
#define PIN_USB_DM        GPIO_PIN_11
#define PIN_USB_DP        GPIO_PIN_12
#define PIN_LED           GPIO_PIN_8

#define USB_CDC_RX_LINE_MAX  64u

#endif
```

- [ ] **Step 5: Create `firmware/include/usbproto.h`**

```c
#ifndef DAYVAULT_USBPROTO_H
#define DAYVAULT_USBPROTO_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    USBPROTO_EVT_NONE = 0,
    USBPROTO_EVT_DFU,
    USBPROTO_EVT_UNKNOWN
} usbproto_event_t;

typedef struct
{
    uint8_t  buf[64];
    uint8_t  len;
    uint8_t  discard;
} usbproto_t;

void usbproto_init(usbproto_t *p);
usbproto_event_t usbproto_feed(usbproto_t *p, uint8_t byte);
usbproto_event_t usbproto_poll(usbproto_t *p);

#endif
```

- [ ] **Step 6: Create `firmware/include/dfu.h`**

```c
#ifndef DAYVAULT_DFU_H
#define DAYVAULT_DFU_H

typedef struct
{
    void (*stop_acquisition)(void);
    void (*close_segment)(void);
    void (*unmount_storage)(void);
} dfu_stop_hooks_t;

void dfu_enter_with_hooks(const dfu_stop_hooks_t *hooks);

#endif
```

- [ ] **Step 7: Write the failing host test** (`firmware/test/test_usbproto/test_usbproto.c`)

```c
#include "unity.h"
#include <string.h>
#include "usbproto.h"

static usbproto_t p;

void setUp(void)
{
    usbproto_init(&p);
}
void tearDown(void)
{
}

static void feed_line(usbproto_t *p, const char *line)
{
    size_t i;
    for (i = 0; line[i]; i++)
        usbproto_feed(p, (uint8_t)line[i]);
}

void test_dfusn_returns_dfu_event(void)
{
    feed_line(&p, "DFU\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}

void test_dfu_crlf_returns_dfu_event(void)
{
    feed_line(&p, "DFU\r\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}

void test_unknown_line_returns_unknown(void)
{
    feed_line(&p, "STAT\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_UNKNOWN, usbproto_poll(&p));
}

void test_empty_line_returns_none(void)
{
    feed_line(&p, "\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
}

void test_partial_line_returns_none(void)
{
    feed_line(&p, "D");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
    feed_line(&p, "FU");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
}

void test_oversize_line_discards_to_newline(void)
{
    uint8_t i;
    for (i = 0; i < 64; i++)
        usbproto_feed(&p, 'X');
    usbproto_feed(&p, '\n');
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_UNKNOWN, usbproto_poll(&p));
    feed_line(&p, "DFU\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}
```

- [ ] **Step 8: Run to verify it fails**

Run: `pio test -e native -f test_usbproto`
Expected: FAIL — `usbproto.h` not found (link/build error).

- [ ] **Step 9: Implement `firmware/src/usbproto.c`**

```c
#include "usbproto.h"
#include <string.h>

void usbproto_init(usbproto_t *p)
{
    memset(p, 0, sizeof(*p));
}

usbproto_event_t usbproto_feed(usbproto_t *p, uint8_t byte)
{
    usbproto_event_t evt = USBPROTO_EVT_NONE;

    if (p->discard)
    {
        if (byte == '\n')
            p->discard = 0;
        return USBPROTO_EVT_NONE;
    }

    if (byte == '\n')
    {
        if (p->len == 3 && memcmp(p->buf, "DFU", 3) == 0)
            evt = USBPROTO_EVT_DFU;
        else if (p->len > 0)
            evt = USBPROTO_EVT_UNKNOWN;
        usbproto_init(p);
        return evt;
    }

    if (p->len >= sizeof(p->buf))
    {
        p->discard = 1;
        return USBPROTO_EVT_NONE;
    }

    p->buf[p->len++] = byte;
    return USBPROTO_EVT_NONE;
}

usbproto_event_t usbproto_poll(usbproto_t *p)
{
    return USBPROTO_EVT_NONE;
}
```

Note: `\r` before `\n` is simply ignored because the trailing `\r` sits at the end of the line; the `\n` comparison still matches since `\r` is stored in `buf` as one byte and `len==4` would break the `len==3` match. To accept both `DFU\n` and `DFU\r\n`, the parser must strip a trailing `\r`: adjust the `\n` handler to compare `len==4 && buf[3]=='\r' ? len=3` semantics. Implementation detail: in the `\n` handler, first drop a trailing `\r` from `buf` before matching:

```c
    if (byte == '\n')
    {
        if (p->len > 0 && p->buf[p->len - 1] == '\r')
            p->len--;
        if (p->len == 3 && memcmp(p->buf, "DFU", 3) == 0)
            evt = USBPROTO_EVT_DFU;
        else if (p->len > 0)
            evt = USBPROTO_EVT_UNKNOWN;
        usbproto_init(p);
        return evt;
    }
```

- [ ] **Step 10: Run to verify it passes**

Run: `pio test -e native -f test_usbproto`
Expected: PASS (6 tests).

- [ ] **Step 11: Verify device env builds**

Run: `pio run -e dayvault`
Expected: SUCCESS (main.c not yet created; empty build is acceptable only if the builder tolerates no main. If the linker errors on missing `main`, create a minimal `firmware/src/main.c` with an empty `int main(void){return 0;}` stub, then Task 2 replaces it.)

- [ ] **Step 12: Commit**

```bash
git add firmware/platformio.ini firmware/boards/dayvault_l452rc.json firmware/include/stm32l4xx_hal_conf.h firmware/include/dayvault_config.h firmware/include/usbproto.h firmware/include/dfu.h firmware/src/usbproto.c firmware/test/test_usbproto/test_usbproto.c
git commit -m "feat(firmware): add PlatformIO scaffold, L452RC board, and usbproto line parser"
```

---

### Task 2: Clock tree + USB device stack (CDC enumeration)

**Files:**
- Create: `firmware/src/main.c`
- Create: `firmware/include/hw_usb.h`
- Create: `firmware/src/hw_usb.c`
- Create: `firmware/include/usbd_conf.h`
- Create: `firmware/include/usbd_desc.h`
- Create: `firmware/src/usbd_desc.c`
- Create: `firmware/src/usbd_cdc_if.c`

**Interfaces:**
- Consumes: `dayvault_config.h` pins; `usbd_conf.h` PCD config.
- Produces: `hw_usb_init(void)` (init PCD + USBD + CDC), `hw_usb_poll(void)` (USBD_LL handler + rx processing), `USBD_CDC_ReceivePacket` glue and an `rx_line_callback(const char *line, size_t len)` hook that Task 3 wires to the parser.

- [ ] **Step 1: Create `firmware/src/main.c`**

```c
#include "stm32l4xx_hal.h"
#include "hw_usb.h"
#include "app.h"

static void SystemClock_Config(void);
void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    hw_usb_init();
    app_run();
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    /* MSI 8 MHz -> PLL (M=1, N=20, R=2) -> 80 MHz SYSCLK, VCO 160 MHz.
       USB FS 48 MHz from PLLSAI1 Q (N=12, Q=2 on shared 8 MHz MSI). L4 main PLL
       Q is even-only (2,4,6,8) so it cannot yield 48 MHz alongside 80 MHz PLLR;
       PLLSAI1Q used instead (verified pattern for L4 USB). */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_7;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();

    /* USB 48 MHz */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLLSAI1;
    PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_MSI;
    PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
    PeriphClkInit.PLLSAI1.PLLSAI1N = 12;
    PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
    PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
    PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
    PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
        Error_Handler();

    HAL_InitTick(TICK_INT_PRIORITY);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
```

Note: `FLASH_LATENCY_4` for 80 MHz on L4 (verified against HAL table). The clock values match the previously verified L452 configuration in the project's history; `PLLSAI1N=12, Q=2` gives 48 MHz USB.

- [ ] **Step 2: Create `firmware/include/usbd_conf.h`**

```c
#ifndef USBD_CONF_H
#define USBD_CONF_H

#include <stdint.h>
#include <string.h>
#include "stm32l4xx.h"

#define USBD_MAX_NUM_INTERFACES  1U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SZ    64U
#define USBD_SELF_POWERED       1U
#define USBD_DEBUG_LEVEL        0U

#define USBD_CDC_INTERFACE_STR_ID 0U
#define USBD_CDC_CLASS_TEMPLATE_ID 0U

/* PCD FIFO / buffer config */
#define PCD_PMA_BUFFER_SIZE      512U

#endif
```

- [ ] **Step 3: Create `firmware/include/usbd_desc.h`**

```c
#ifndef USBD_DESC_H
#define USBD_DESC_H

#include "usbd_def.h"

#define DEVICE_ID1    (0x1FFF7590U)
#define DEVICE_ID2    (0x1FFF7594U)
#define DEVICE_ID3    (0x1FFF7598U)

#define USB_SIZ_STRING 64U

extern USBD_DescriptorsTypeDef DayVault_Desc;

#endif
```

Note: The unique-ID addresses `0x1FFF7590` are the L452 factory memory locations (verify against `stm32l452xx.h`/datasheet during implementation; adjust if the CMSIS header defines `UID_BASE` differently — prefer `UID_BASE` macro if present).

- [ ] **Step 4: Create `firmware/src/usbd_desc.c`**

```c
#include "usbd_desc.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "stm32l4xx_hal.h"

static uint8_t USBD_DeviceDesc[18] = {
    0x12,                        /* bLength */
    USB_DESC_TYPE_DEVICE,        /* bDescriptorType */
    0x00, 0x02,                  /* bcdUSB 2.00 */
    0x02,                        /* bDeviceClass: CDC */
    0x00,                        /* bDeviceSubClass */
    0x00,                        /* bDeviceProtocol */
    USB_MAX_EP0_SIZE,            /* bMaxPacketSize0 */
    0x83, 0x00,                  /* idVendor (0x0083) */
    0x11, 0x00,                  /* idProduct (0x0011) */
    0x00, 0x02,                  /* bcdDevice */
    1,                           /* iManufacturer */
    2,                           /* iProduct */
    3,                           /* iSerialNumber */
    0x01                         /* bNumConfigurations */
};

static void Get_SerialNum(void)
{
    uint32_t uid1 = HAL_GetUIDw0();
    uint32_t uid2 = HAL_GetUIDw1();
    uint32_t uid3 = HAL_GetUIDw2();
    char buf[25];
    int n = snprintf(buf, sizeof(buf), "%08lx%08lx%08lx",
                     (unsigned long)uid1, (unsigned long)uid2, (unsigned long)uid3);
    (void)n;
}

static uint8_t *USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_DeviceDesc);
    return USBD_DeviceDesc;
}

static uint8_t *USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t langid[4] = { 4, USB_DESC_TYPE_STRING, 0x09, 0x04 };
    *length = sizeof(langid);
    return langid;
}

static uint8_t *USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t str[USB_SIZ_STRING];
    USBD_GetString((uint8_t *)"DayVault", str, length);
    return str;
}

static uint8_t *USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t str[USB_SIZ_STRING];
    USBD_GetString((uint8_t *)"DayVault Recorder", str, length);
    return str;
}

static uint8_t *USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t str[USB_SIZ_STRING];
    Get_SerialNum();
    return str;
}

static uint8_t *USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; (void)length;
    return 0;
}

static uint8_t *USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; (void)length;
    return 0;
}

USBD_DescriptorsTypeDef DayVault_Desc =
{
    USBD_DeviceDescriptor,
    USBD_LangIDStrDescriptor,
    USBD_ManufacturerStrDescriptor,
    USBD_ProductStrDescriptor,
    USBD_SerialStrDescriptor,
    USBD_ConfigStrDescriptor,
    USBD_InterfaceStrDescriptor
};
```

Note: `Get_SerialNum()` above is a skeleton that formats UID into a local buffer (not yet written into the descriptor string). During implementation, build a real UTF-16 string with `USBD_GetString` using the formatted hex UID. Simplify to a fixed string `"00000000"` if a stable serial is not needed for this milestone; keep the code compiling and warning-free.

- [ ] **Step 5: Create `firmware/include/hw_usb.h`**

```c
#ifndef DAYVAULT_HW_USB_H
#define DAYVAULT_HW_USB_H

#include <stddef.h>

typedef void (*rx_line_cb)(const char *line, size_t len);

void hw_usb_init(void);
void hw_usb_poll(void);
void hw_usb_set_rx_line_callback(rx_line_cb cb);

#endif
```

- [ ] **Step 6: Create `firmware/src/hw_usb.c` (PCD + USBD + CDC glue)**

```c
#include "stm32l4xx_hal.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "usbd_desc.h"
#include "hw_usb.h"
#include "dayvault_config.h"
#include <string.h>

static PCD_HandleTypeDef hpcd;
static USBD_HandleTypeDef hUsbDevice;
static rx_line_cb rx_cb = 0;

static uint8_t cdc_rx_buf[64];
static char line_buf[64];
static size_t line_len = 0;

void hw_usb_set_rx_line_callback(rx_line_cb cb)
{
    rx_cb = cb;
}

void hw_usb_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_USB_CLK_ENABLE();

    /* PA11 = USB_DM, PA12 = USB_DP */
    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_USB_DM | PIN_USB_DP;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF10_USB;
    HAL_GPIO_Init(GPIOA, &g);

    HAL_PWREx_EnableVddUSB();

    hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8;
    hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.ep0_mps = USB_MAX_EP0_SIZE;
    hpcd.Init.low_power_enable = DISABLE;
    hpcd.Init.lpm_enable = DISABLE;
    hpcd.Init.battery_charging_enable = DISABLE;
    HAL_PCD_Init(&hpcd);

    USBD_Init(&hUsbDevice, &DayVault_Desc, 0);
    USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDevice, &usbd_cdc_if_fops);
    USBD_Start(&hUsbDevice);
}

void hw_usb_poll(void)
{
    /* PCD IRQ is handled by HAL_PCD_IRQHandler from the USB_IRQHandler;
       this poll is reserved for non-ISR processing if needed. */
}

/* --- CDC glue (called from usbd_cdc_if.c) --- */
void cdc_rx_bytes(const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
    {
        if (line_len < sizeof(line_buf))
            line_buf[line_len++] = (char)data[i];
        if (data[i] == '\n')
        {
            if (rx_cb)
                rx_cb(line_buf, line_len);
            line_len = 0;
            memset(line_buf, 0, sizeof(line_buf));
        }
    }
}

/* ISR */
void USB_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd);
}

/* MSP */
void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
    (void)hpcd;
}

/* USBD callbacks -> HAL PCD low-level */
void USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
}

void USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
}

void USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    HAL_PCD_Start(&hpcd);
}

void USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    HAL_PCD_Stop(&hpcd);
}

void USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint16_t ep_mps,
                    uint8_t ep_type)
{
    (void)pdev;
    HAL_PCD_EP_Open(&hpcd, ep_addr, ep_mps, ep_type);
}

void USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_Close(&hpcd, ep_addr);
}

void USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_Flush(&hpcd, ep_addr);
}

void USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_SetStall(&hpcd, ep_addr);
}

void USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_ClrStall(&hpcd, ep_addr);
}

uint32_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    return HAL_PCD_EP_IsStall(&hpcd, ep_addr);
}

void USBD_LL_EP0Out(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len)
{
    (void)pdev;
    HAL_PCD_EP_Receive(&hpcd, 0, pbuf, len);
}

void USBD_LL_EP0In(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len)
{
    (void)pdev;
    HAL_PCD_EP_Transmit(&hpcd, 0x80, pbuf, len);
}

void USBD_LL_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    (void)pdev; (void)epnum;
}

void USBD_LL_IsoOUTIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    (void)pdev; (void)epnum;
}

void USBD_LL_SetupStage(USBD_HandleTypeDef *pdev, uint8_t *psetup)
{
    (void)pdev;
    HAL_PCD_SetupStage(&hpcd, psetup);
}

void USBD_LL_DataOutStage(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *pdata)
{
    (void)pdev;
    HAL_PCD_DataOutStage(&hpcd, epnum, pdata);
}

void USBD_LL_DataInStage(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *pdata)
{
    (void)pdev;
    HAL_PCD_DataInStage(&hpcd, epnum, pdata);
}

void USBD_LL_Reset(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    HAL_PCD_Reset(&hpcd);
}

void USBD_LL_SetSpeed(USBD_HandleTypeDef *pdev, uint8_t speed)
{
    (void)pdev; (void)speed;
}

void USBD_LL_Suspend(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
}

void USBD_LL_Resume(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
}

void USBD_LL_SOF(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
}

void USBD_LL_Unlock(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
}

void USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t addr)
{
    (void)pdev;
    HAL_PCD_SetAddress(&hpcd, addr);
}
```

Note: `usbd_cdc_if_fops` and `USBD_CDC_RegisterInterface` come from the ST `usbd_cdc_if` template pattern; the project's `usbd_cdc_if.c` (Task 2 Step 7) provides the `USBD_CDC_ITF_*` callbacks and the `usbd_cdc_if_fops` struct. `USBD_LL_*` signatures must match the installed `usbd_def.h`/`usbd_ioreq.h` — verify exact parameter lists against the framework headers during compile iteration.

- [ ] **Step 7: Create `firmware/src/usbd_cdc_if.c`**

```c
#include "usbd_cdc_if.h"
#include "usbd_cdc.h"
#include "usbd_def.h"
#include "hw_usb.h"
#include <string.h>

static int8_t CDC_Init(void)
{
    return 0;
}

static int8_t CDC_DeInit(void)
{
    return 0;
}

static int8_t CDC_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)cmd; (void)pbuf; (void)length;
    return 0;
}

static int8_t CDC_Receive(uint8_t *pbuf, uint32_t *Len)
{
    uint8_t *rx = pbuf;
    uint32_t len = *Len;
    cdc_rx_bytes(rx, len);
    return 0;
}

static int8_t CDC_Transmit(uint8_t *pbuf, uint16_t len, uint8_t ep_num)
{
    (void)pbuf; (void)len; (void)ep_num;
    return 0;
}

USBD_CDC_ItfTypeDef usbd_cdc_if_fops =
{
    CDC_Init,
    CDC_DeInit,
    CDC_Control,
    CDC_Receive,
    CDC_Transmit
};
```

Note: `cdc_rx_bytes` is declared in `hw_usb.c` and exposed to this file — add `void cdc_rx_bytes(const uint8_t *data, size_t len);` declaration in `hw_usb.h` (or a small `usbd_cdc_if.h`). The `CDC_Receive` must re-arm the reception after processing; the exact `USBD_CDC_ReceivePacket(&hUsbDevice)` call and the `usbd_cdc_if_fops` struct layout must match the installed `usbd_cdc.h`. Verify against the framework headers during compile.

- [ ] **Step 8: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS with zero warnings. Iterate on USBD API names until it links; USB PCD + CDC from the auto-built library must resolve.

- [ ] **Step 9: Commit**

```bash
git add firmware/src/main.c firmware/include/hw_usb.h firmware/src/hw_usb.c firmware/include/usbd_conf.h firmware/include/usbd_desc.h firmware/src/usbd_desc.c firmware/src/usbd_cdc_if.c
git commit -m "feat(firmware): add clock tree and USB CDC device stack"
```

---

### Task 3: Wire CDC line → parser → dfu_enter

**Files:**
- Create: `firmware/src/dfu.c`
- Create: `firmware/include/app.h`
- Create: `firmware/src/app.c`
- Modify: `firmware/platformio.ini` (add `-<test/*>` to dayvault env build_src_filter)
- Modify: `firmware/src/main.c` (call `app_init()` before `hw_usb_init()`, or reorder — see step)

**Interfaces:**
- Consumes: `usbproto_t` (Task 1), `hw_usb_set_rx_line_callback` (Task 2), `dfu_enter_with_hooks` (this task).
- Produces: `app_init(void)`, `app_run(void)`, `dfu_enter_with_hooks` implemented in `dfu.c`.

- [ ] **Step 1: Implement `firmware/src/dfu.c`**

```c
#include "dfu.h"
#include "stm32l4xx_hal.h"

#define SYSTEM_MEMORY_BASE 0x1FFF0000u
#define SRAM_LOW_BOUNDARY  0x20000000u

void dfu_enter_with_hooks(const dfu_stop_hooks_t *hooks)
{
    uint32_t msp;

    if (hooks)
    {
        if (hooks->stop_acquisition) hooks->stop_acquisition();
        if (hooks->close_segment)    hooks->close_segment();
        if (hooks->unmount_storage)  hooks->unmount_storage();
    }

    HAL_Delay(20);

    /* Tear down USB before jumping so the ROM bootloader can re-enumerate. */
    hw_usb_deinit();
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    __disable_irq();

    msp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
    if ((msp & 0xFFF00000u) != SRAM_LOW_BOUNDARY)
        while (1) { }   /* invalid bootloader stack — do not jump */

    __set_MSP(msp);
    ((void (*)(void)) * (volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4u))();
}
```

Note: `hpcd` is declared `static` inside `hw_usb.c`, so `dfu.c` calls the exposed teardown function `hw_usb_deinit(void)` (declared in `hw_usb.h`, implemented in `hw_usb.c`, calling `HAL_PCD_DeInit(&hpcd)` and `HAL_PCD_MspDeInit`). The MSP check uses `& 0xFFF00000 == 0x20000000` (any RAM address), sufficient for validation.

- [ ] **Step 2: Create `firmware/include/app.h`**

```c
#ifndef DAYVAULT_APP_H
#define DAYVAULT_APP_H

void app_init(void);
void app_run(void);

#endif
```

- [ ] **Step 3: Create `firmware/src/app.c`**

```c
#include "app.h"
#include "usbproto.h"
#include "hw_usb.h"
#include "dfu.h"

static usbproto_t proto;
static usbproto_event_t pending_evt = USBPROTO_EVT_NONE;

static void on_rx_line(const char *line, size_t len)
{
    size_t i;
    usbproto_init(&proto);
    for (i = 0; i < len; i++)
    {
        usbproto_event_t evt = usbproto_feed(&proto, (uint8_t)line[i]);
        if (evt != USBPROTO_EVT_NONE)
            pending_evt = evt;
    }
}

static void noop(void)
{
}

void app_init(void)
{
    static const dfu_stop_hooks_t hooks = {
        noop,   /* stop_acquisition */
        noop,   /* close_segment */
        noop    /* unmount_storage */
    };
    (void)hooks;

    usbproto_init(&proto);
    hw_usb_set_rx_line_callback(on_rx_line);
}

void app_run(void)
{
    for (;;)
    {
        hw_usb_poll();

        if (pending_evt == USBPROTO_EVT_DFU)
        {
            static const dfu_stop_hooks_t hooks = {
                noop, noop, noop
            };
            pending_evt = USBPROTO_EVT_NONE;
            dfu_enter_with_hooks(&hooks);
        }
    }
}
```

Note: The parser expects `\n` terminators; the USB line assembly in `hw_usb.c` (`cdc_rx_bytes`) currently strips nothing and passes the whole line including `\n` to `on_rx_line`. Feeding the `\n` through `usbproto_feed` is correct (it triggers the match). Keep the assembly as-is. The `on_rx_line` feeds the line; `usbproto_poll` remains for potential deferred reads but is not required in this flow.

- [ ] **Step 4: Update `firmware/platformio.ini`** so the device env excludes test sources

```ini
[env:dayvault]
platform = ststm32
board = dayvault_l452rc
framework = stm32cube
board_build.flash_size = 256KB
build_type = release
monitor_speed = 115200
build_flags =
    -Os
    -std=gnu99
    -DUSBD_ACTIVATE_CDC=1
build_src_filter =
    -<test/*>
```

- [ ] **Step 5: Update `firmware/src/main.c`** call order

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    app_init();
    hw_usb_init();
    app_run();
}
```

- [ ] **Step 6: Compile-verify device**

Run: `pio run -e dayvault`
Expected: SUCCESS, zero warnings.

- [ ] **Step 7: Verify native tests still pass**

Run: `pio test -e native -f test_usbproto`
Expected: PASS (6 tests).

- [ ] **Step 8: Commit**

```bash
git add firmware/src/dfu.c firmware/include/app.h firmware/src/app.c firmware/src/main.c firmware/platformio.ini firmware/include/hw_usb.h firmware/src/hw_usb.c
git commit -m "feat(firmware): wire CDC DFU command to system-memory bootloader jump"
```

---

### Task 4: Final verification and cleanup

**Files:**
- Modify: none expected (cleanup only)

**Interfaces:**
- Verifies the whole milestone build and tests.

- [ ] **Step 1: Clean build + full test run**

```bash
pio run -e dayvault -t clean
pio run -e dayvault
pio test -e native
```

Expected: device SUCCESS zero warnings; native 6/6 PASS.

- [ ] **Step 2: Check git state**

```bash
git status
git log --oneline -5
```

Expected: clean tree, only the three firmware commits ahead of the docs commit.

- [ ] **Step 3: Report flash/RAM usage**

Run: `pio run -e dayvault -v 2>&1 | Select-String "Memory Usage|Flash:|RAM:"`
Record the reported Flash and RAM percentages in the completion message.

- [ ] **Step 4: Commit any stragglers**

```bash
git add -A
git commit -m "chore(firmware): final verification pass"
```

(Only commit if there are uncommitted changes; otherwise skip.)

---

## Self-Review Notes

- **Spec coverage (Docs/09):** §3 trigger → usbproto `DFU` match (Task 1). §4 sequence → dfu.c hooks + HAL_DeInit + jump (Task 3). §4.4 settle delay → `HAL_Delay(20)` (Task 3). §5 interface → `dfu_enter_with_hooks` + `dfu_stop_hooks_t` (Task 1 header, Task 3 impl). §6 safety → vector-table MSP check, no flash writes (Task 3). §7 testing → host test for parser (Task 1), compile-only for dfu (Task 3), board bring-up deferred. §8 scope → recording pipeline explicitly out of scope.
- **Placeholder scan:** Get_SerialNum in Task 2 Step 4 has a documented simplification path (fixed string acceptable) — acceptable, marked. `cdc_rx_bytes` cross-file declaration noted. No TBDs.
- **Type consistency:** `usbproto_event_t`, `usbproto_feed`, `usbproto_poll` signatures consistent across Task 1 (header+impl) and Task 3 (app.c). `dfu_stop_hooks_t` field names consistent between dfu.h and dfu.c. `hw_usb_set_rx_line_callback(rx_line_cb)` matches between Task 2 (hw_usb.h) and Task 3 (app.c). `hw_usb_deinit` is declared in `hw_usb.h` (Task 2) and implemented in `hw_usb.c`; `dfu.c` (Task 3) calls it. Task 2's `hw_usb.h` block above must include `void hw_usb_deinit(void);` — the implementer adds it alongside `hw_usb_set_rx_line_callback`.
