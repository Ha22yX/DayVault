#pragma once
#include "stm32l4xx_hal.h"
#include "PdmRate.h"

#define PIN_SD_CS       GPIO_PIN_4
#define PIN_SD_CS_PORT  GPIOA
#define PIN_SD_SCK      GPIO_PIN_5
#define PIN_SD_MISO     GPIO_PIN_6
#define PIN_SD_MOSI     GPIO_PIN_7

#define PIN_USB_DETECT_PIN   GPIO_PIN_9
#define PIN_USB_DETECT_PORT  GPIOA

#define PIN_PDM_CLK       GPIO_PIN_2
#define PIN_PDM_CLK_PORT  GPIOC
#define PIN_PDM_DATA      GPIO_PIN_12
#define PIN_PDM_DATA_PORT GPIOB

#define PDM_CKOUT_DIVIDER PDM_PRODUCTION_CKOUT_DIVIDER
#define PDM_OSR           PDM_PRODUCTION_OSR
#define PDM_GAIN          128u
#define PDM_HALF_SAMPLES  512u
#define PDM_RING_BYTES    (PDM_HALF_SAMPLES * 2u * 8u)   /* 8 KB ring */

#if PDM_CKOUT_DIVIDER == 0u
#error "PDM_CKOUT_DIVIDER must be non-zero"
#endif

#if PDM_OSR == 0u
#error "PDM_OSR must be non-zero"
#endif

#if PDM_CLOCK_HZ(PDM_DFSDM_SOURCE_HZ, PDM_CKOUT_DIVIDER) < 1100000u || \
    PDM_CLOCK_HZ(PDM_DFSDM_SOURCE_HZ, PDM_CKOUT_DIVIDER) > 4800000u
#error "PDM clock must remain in the SPH0655 normal-mode range"
#endif

#define AUDIO_SAMPLE_RATE PDM_PCM_RATE_HZ(PDM_DFSDM_SOURCE_HZ, PDM_CKOUT_DIVIDER, PDM_OSR)
#define AUDIO_CHANNELS    1u
#define AUDIO_BITS        16u

static_assert(AUDIO_SAMPLE_RATE == 16000u,
              "Opus recording requires exact 16 kHz PCM");

#define REC_DIR_STR "REC"
#define REC_EXT_STR "OPUS"
#define REC_SEQ_MAX 999u
