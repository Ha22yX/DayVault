#pragma once
#include <stdint.h>
#include <stddef.h>

#define PIN_USB_DETECT   GPIO_PIN_9
#define PIN_LED          GPIO_PIN_8

#define PIN_SD_CS       GPIO_PIN_4
#define PIN_SD_CS_PORT  GPIOA
#define PIN_SD_SCK      GPIO_PIN_5
#define PIN_SD_MISO     GPIO_PIN_6
#define PIN_SD_MOSI     GPIO_PIN_7

#define PIN_PDM_CLK       GPIO_PIN_2
#define PIN_PDM_CLK_PORT  GPIOC
#define PIN_PDM_DATA      GPIO_PIN_12
#define PIN_PDM_DATA_PORT GPIOB

#define AUDIO_SAMPLE_RATE 16000u
#define AUDIO_CHANNELS    1u
#define AUDIO_BITS        16u

#define PDM_CKOUT_DIVIDER 39u
#define PDM_OSR           128u
#define PDM_HALF_SAMPLES  512u
#define PDM_RING_BYTES    (PDM_HALF_SAMPLES * 2u * 16u)   /* 16 KB ring */

#define REC_DIR_STR "REC"
#define REC_EXT_STR "WAV"
#define REC_SEQ_MAX 999u

#define USB_VID 0x0483u
#define USB_PID 0x5741u
#define CDC_RX_LINE_MAX 64u
