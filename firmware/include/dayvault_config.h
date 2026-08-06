#ifndef DAYVAULT_CONFIG_H
#define DAYVAULT_CONFIG_H

/* Pin mapping - must match Docs/02 and netlist (do not change) */
#define PIN_SD_CS        GPIO_PIN_4
#define PIN_SD_CS_PORT   GPIOA
#define PIN_SD_SCK       GPIO_PIN_5
#define PIN_SD_MISO      GPIO_PIN_6
#define PIN_SD_MOSI      GPIO_PIN_7
#define PIN_USB_DM       GPIO_PIN_11
#define PIN_USB_DP       GPIO_PIN_12
#define PIN_USB_DETECT   GPIO_PIN_9
#define PIN_BAT_SENSE    GPIO_PIN_0
#define PIN_PDM_CLK      GPIO_PIN_2   /* PC2 DFSDM1_CKOUT */
#define PIN_PDM_CLK_PORT GPIOC
#define PIN_PDM_DATA     GPIO_PIN_12  /* PB12 DFSDM1_DATIN1 */
#define PIN_PDM_DATA_PORT GPIOB
#define PIN_LED          GPIO_PIN_8

/* Battery thresholds (mV) - tune on board per Docs/04 */
#define BAT_WARNING_MV    3500u
#define BAT_CRITICAL_MV   3300u
#define BAT_RECOVERY_MV   3550u
#define BAT_ADC_AVG_COUNT 16u

/* Recording */
#define AUDIO_SAMPLE_RATE 16000u
#define AUDIO_CHANNELS    1u
#define SEGMENT_SECONDS   900u
#define WAV_SYNC_INTERVAL_MS 10000u
#define SEGMENT_PREALLOC_BYTES (AUDIO_SAMPLE_RATE * 2u * SEGMENT_SECONDS)
#define PDM_HALF_SAMPLES  1024u
#define PDM_RING_BYTES    (PDM_HALF_SAMPLES * 2u * 8u)

/* Low power */
#define STANDBY_WAKE_SEC  30u

/* RTC backup register indices */
#define BKP_IDX_MAGIC   1u
#define BKP_IDX_BOOT    2u
#define RTC_VALID_MAGIC 0xDA27u

/* DFSDM CKOUT frequency for 16 kHz output at OSR 128 */
#define PDM_CKOUT_HZ     2048000u

#endif
