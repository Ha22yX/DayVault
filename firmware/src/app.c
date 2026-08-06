#include "stm32l4xx_hal.h"
#include "app.h"
#include "dayvault_config.h"
#include "hw_rtc.h"
#include "hw_adc.h"
#include "hw_gpio.h"
#include "hw_spi_sd.h"
#include "hw_dfsdm.h"
#include "hw_usb.h"
#include "hw_iwdg.h"
#include "hw_pwr.h"
#include "power_state.h"
#include "segmgr.h"
#include "wav.h"
#include "adpcm.h"
#include "eventlog.h"
#include "ringbuf.h"
#include "timeutil.h"
#include "diskio_sd.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

static FATFS fs;
static FIL file;
static pstate_machine_t psm;
static pstate_actions_t psm_actions;
static segmgr_t seg;
static ringbuf_t audio_rb;
static uint8_t audio_buf[PDM_RING_BYTES];
static uint8_t sd_buf[512];
static uint32_t overrun_count = 0;
static uint8_t last_usb = 0;
static uint32_t last_bat_tick = 0;
static uint32_t last_sync_tick = 0;
static uint8_t seg_open = 0;
static int sd_mounted = 0;

static const wav_config_t wav_cfg =
{
    WAV_PCM_FORMAT,
    AUDIO_SAMPLE_RATE,
    AUDIO_CHANNELS,
    16,
    AUDIO_CHANNELS * 16 / 8,
    AUDIO_SAMPLE_RATE * (AUDIO_CHANNELS * 16 / 8)
};

static void close_segment(void);

static void on_audio_samples(const int16_t *samples, uint16_t count)
{
    size_t bytes = (size_t)count * 2u;
    /* runs in DMA1_Channel4 ISR context; ringbuf ops are not atomic w.r.t.
       the superloop drain, board bring-up may need a short critical section */
    if (ringbuf_free(&audio_rb) < bytes)
    {
        overrun_count++;
        return;
    }
    ringbuf_write(&audio_rb, (const uint8_t *)samples, bytes);
}

static void on_transition(pstate_t from, pstate_t to, pevent_t evt)
{
    (void)from; (void)evt;
    switch (to)
    {
    case PSTATE_IDLE:
        close_segment();
        if (sd_mounted)
        {
            f_mount(0, "SD:", 0);
            sd_mounted = 0;
        }
        break;
    case PSTATE_STOPPING:
        hw_dfsdm_stop();
        break;
    case PSTATE_STANDBY:
        close_segment();
        hw_pwr_set_wake_period(STANDBY_WAKE_SEC);
        hw_pwr_enter_standby();
        break;
    default:
        break;
    }
}

void app_init(void)
{
    utc_time_t t;

    psm_actions.on_transition = on_transition;

    hw_gpio_init();
    hw_rtc_init();
    hw_adc_init();
    hw_dfsdm_init();
    hw_usb_init();
    hw_iwdg_init();

    hw_rtc_bump_boot_counter();

    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    segmgr_init(&seg, SEGMENT_SECONDS, SEGMENT_PREALLOC_BYTES);
    pstate_init(&psm, &psm_actions);
    hw_dfsdm_set_callback(on_audio_samples);

    if (!hw_rtc_is_time_valid())
        t = (utc_time_t){2026, 1, 1, 0, 0, 0};   /* boot counter fallback */
    else
        hw_rtc_get_time(&t);

    if (hw_gpio_usb_detect())
        pstate_handle(&psm, PEVT_USB_ATTACH);
    else if (hw_adc_read_battery_mv() <= BAT_CRITICAL_MV)
        pstate_handle(&psm, PEVT_BATTERY_CRITICAL);
    else
    {
        if (hw_sd_init())
        {
            if (FATFS_LinkDriverEx(&Diskio_SD_Driver, "SD:", 0) == 0)
            {
                if (f_mount(&fs, "SD:", 1) == FR_OK)
                    sd_mounted = 1;
            }
        }
        pstate_handle(&psm, PEVT_BOOT_OK);
    }
    last_usb = hw_gpio_usb_detect();
    last_bat_tick = HAL_GetTick();
    last_sync_tick = HAL_GetTick();
}

static void ensure_parent_dirs(const char *path)
{
    size_t i;
    size_t len = strlen(path);
    for (i = 1; i < len; i++)
    {
        if (path[i] == '/')
        {
            char buf[128];
            memcpy(buf, path, i);
            buf[i] = 0;
            f_mkdir(buf);
        }
    }
}

static void open_segment(void)
{
    utc_time_t now;
    char sub[80];
    char fname[48];
    char path[140];
    UINT wr = 0;

    if (!sd_mounted)
    {
        if (f_mount(&fs, "SD:", 1) == FR_OK)
            sd_mounted = 1;
    }
    if (!sd_mounted)
        return;

    hw_rtc_get_time(&now);
    segmgr_open(&seg, &now);
    segmgr_build_name(&seg, &now, fname, sizeof(fname));
    if (hw_rtc_is_time_valid())
        timeutil_make_day_path(&now, sub, sizeof(sub));
    else
        timeutil_make_unsynced_path(hw_rtc_boot_counter(), sub, sizeof(sub));
    snprintf(path, sizeof(path), "SD:/%s/%s", sub, fname);
    ensure_parent_dirs(path);
    if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        wav_build_header(sd_buf, &wav_cfg, 0);
        f_write(&file, sd_buf, (UINT)wav_header_size(&wav_cfg), &wr);
        seg_open = 1;
    }
}

static void close_segment(void)
{
    UINT wr = 0;
    uint32_t data_bytes;
    if (!seg_open)
        return;
    data_bytes = (uint32_t)f_size(&file) - (uint32_t)wav_header_size(&wav_cfg);
    wav_patch_sizes(sd_buf, &wav_cfg, data_bytes);
    f_lseek(&file, 0);
    f_write(&file, sd_buf, (UINT)wav_header_size(&wav_cfg), &wr);
    f_close(&file);
    segmgr_close(&seg);
    seg_open = 0;
}

static void drain_audio(void)
{
    static uint8_t pcm_block[1024];
    UINT got;
    UINT wr = 0;
    got = (UINT)ringbuf_read(&audio_rb, pcm_block, sizeof(pcm_block));
    got &= (UINT)~1u;   /* byte-align to 16-bit samples */
    if (got < 2u)
        return;
    if (seg_open)
        f_write(&file, pcm_block, got, &wr);
}

void app_run(void)
{
    uint32_t now_tick;
    for (;;)
    {
        now_tick = HAL_GetTick();
        hw_iwdg_feed();

        /* battery periodic sampling; on L4 each read restarts single-shot
           conversion inside hw_adc_read_battery_mv (Task 3 note) */
        if (now_tick - last_bat_tick >= 250u)
        {
            uint16_t mv = hw_adc_read_battery_mv();
            if (mv <= BAT_CRITICAL_MV)
                pstate_handle(&psm, PEVT_BATTERY_CRITICAL);
            else if (mv <= BAT_WARNING_MV)
                pstate_handle(&psm, PEVT_BATTERY_WARNING);
            else if (mv >= BAT_RECOVERY_MV)
                pstate_handle(&psm, PEVT_BATTERY_OK);
            last_bat_tick = now_tick;
        }

        /* USB_DETECT debounce (>= 5 ms) */
        {
            uint8_t now_usb = hw_gpio_usb_detect();
            if (now_usb != last_usb)
            {
                HAL_Delay(5);
                now_usb = hw_gpio_usb_detect();
                if (now_usb != last_usb)
                {
                    pstate_handle(&psm, now_usb ? PEVT_USB_ATTACH : PEVT_USB_DETACH);
                    last_usb = now_usb;
                }
            }
        }

        if (pstate_get(&psm) == PSTATE_RECORDING || pstate_get(&psm) == PSTATE_RECORDING_LOW)
        {
            if (!seg_open)
                open_segment();

            if (seg_open)
            {
                utc_time_t now;
                hw_rtc_get_time(&now);
                if (segmgr_should_rotate(&seg, &now))
                    close_segment();
                drain_audio();

                /* periodic WAV header sync (10 s) */
                if (now_tick - last_sync_tick >= WAV_SYNC_INTERVAL_MS)
                {
                    f_sync(&file);
                    last_sync_tick = now_tick;
                }
            }
        }

        hw_usb_poll();
    }
}
