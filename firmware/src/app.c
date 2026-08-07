#include "app.h"
#include "usbproto.h"
#include "hw_usb.h"
#include "dfu.h"
#include "rec_mgr.h"
#include "wav.h"
#include "ringbuf.h"
#include "hw_dfsdm.h"
#include "hw_spi_sd.h"
#include "diskio_sd.h"
#include "ff.h"
#include "dayvault_config.h"
#include <stdio.h>
#include <string.h>

#define USB_DETECT_DEBOUNCE_MS 10u

static FATFS fs;
static FIL file;
static rec_mgr_t rec;
static ringbuf_t audio_rb;
static uint8_t audio_buf[PDM_RING_BYTES];
static wav_config_t wav_cfg;
static uint32_t seq = 1;
static uint8_t file_open = 0;

static usbproto_t proto;
static volatile usbproto_event_t pending_evt = USBPROTO_EVT_NONE;

static GPIO_PinState usb_level;
static uint8_t usb_armed;
static GPIO_PinState usb_pend_level;
static uint32_t usb_pend_tick;

static void on_rx_line(const char *line, size_t len)
{
    size_t i;
    usbproto_init(&proto);
    for (i = 0; i < len; i++)
    {
        usbproto_event_t evt = usbproto_feed(&proto, (uint8_t)line[i]);
        if (evt == USBPROTO_EVT_DFU)
            pending_evt = USBPROTO_EVT_DFU;
    }
}

static void dfsdm_start(void) { hw_dfsdm_start(); }
static void dfsdm_stop(void) { hw_dfsdm_stop(); }

static void fs_mount_ok(void)
{
    FATFS_LinkDriverEx(&Diskio_SD_Driver, "SD:", 0);
    f_mount(&fs, "SD:", 1);
}

static void fs_unmount(void)
{
    f_mount(NULL, "SD:", 0);
    FATFS_UnLinkDriver("SD:");
}

static void finalize_wav(void)
{
    uint8_t block[512];
    uint32_t data_bytes;
    uint8_t hdr[44];
    UINT wr;
    if (!file_open)
        return;
    while (ringbuf_used(&audio_rb) > 0)
    {
        UINT got = (UINT)ringbuf_read(&audio_rb, block, sizeof(block));
        got &= ~3u;   /* 4-byte stereo frame alignment */
        if (got == 0)
            break;
        f_write(&file, block, got, &wr);
    }
    data_bytes = (uint32_t)f_tell(&file) - 44u;
    wav_patch_sizes(hdr, &wav_cfg, data_bytes);
    f_lseek(&file, 0);
    f_write(&file, hdr, 44, &wr);
    f_sync(&file);
    f_close(&file);
    file_open = 0;
}

static uint32_t parse_rec_number(const char *s)
{
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9')
    {
        n = n * 10u + (uint32_t)(*s - '0');
        if (n > REC_SEQ_MAX)
            n = REC_SEQ_MAX;
        s++;
    }
    return n;
}

static uint32_t scan_sequence(void)
{
    DIR dir;
    FILINFO fno;
    uint32_t max_num = 0;
    if (f_opendir(&dir, "SD:/") != FR_OK)
        return 1;
    for (;;)
    {
        if (f_readdir(&dir, &fno) != FR_OK)
            break;
        if (fno.fname[0] == 0)
            break;
        if ((fno.fattrib & AM_DIR) != 0)
            continue;
        if (strncmp(fno.fname, REC_DIR_STR, strlen(REC_DIR_STR)) != 0)
            continue;
        {
            const char *dot = strrchr(fno.fname, '.');
            if (dot == 0 || strcmp(dot + 1, REC_EXT_STR) != 0)
                continue;
        }
        {
            uint32_t n = parse_rec_number(fno.fname + strlen(REC_DIR_STR));
            if (n > max_num)
                max_num = n;
        }
    }
    f_closedir(&dir);
    if (max_num >= REC_SEQ_MAX)
        return 1;
    return max_num + 1;
}

static void open_next_file(void)
{
    char name[24];
    UINT wr;
    snprintf(name, sizeof(name), "SD:/%s%03u.%s", REC_DIR_STR, (unsigned)seq, REC_EXT_STR);
    if (f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        uint8_t hdr[44];
        wav_build_header(hdr, &wav_cfg, 0);
        f_write(&file, hdr, 44, &wr);
        file_open = 1;
    }
    seq++;
    if (seq > REC_SEQ_MAX)
        seq = 1;
}

static void pump_audio(void)
{
    static uint8_t block[512];
    UINT got, wr;
    if (!file_open)
        return;
    got = (UINT)ringbuf_read(&audio_rb, block, sizeof(block));
    got &= ~3u;   /* 4-byte stereo frame alignment */
    if (got)
        f_write(&file, block, got, &wr);
}

static const rec_actions_t rec_actions = {
    dfsdm_start,
    dfsdm_stop,
    finalize_wav,
    fs_mount_ok,
    fs_unmount,
    hw_usb_enter_msc,
    hw_usb_exit_msc
};

static void usb_detect_poll(void)
{
    GPIO_PinState raw = HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT);
    uint32_t now;

    if (raw == usb_level)
    {
        usb_armed = 0;
        return;
    }

    if (!usb_armed)
    {
        usb_armed = 1;
        usb_pend_level = raw;
        usb_pend_tick = HAL_GetTick();
        return;
    }

    if (raw != usb_pend_level)
    {
        usb_armed = 0;
        return;
    }

    now = HAL_GetTick();
    if ((uint32_t)(now - usb_pend_tick) >= USB_DETECT_DEBOUNCE_MS)
    {
        usb_level = raw;
        usb_armed = 0;
        if (raw == GPIO_PIN_SET)
            rec_mgr_event(&rec, REC_EVT_USB_ATTACH);
        else
            rec_mgr_event(&rec, REC_EVT_USB_DETACH);
    }
}

void app_init(void)
{
    GPIO_InitTypeDef g = {0};
    GPIO_PinState detected;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = PIN_USB_DETECT;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    wav_cfg.format = WAV_PCM_FORMAT;
    wav_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    wav_cfg.channels = AUDIO_CHANNELS;
    wav_cfg.bits = AUDIO_BITS;
    wav_cfg.block_align = (uint16_t)(AUDIO_CHANNELS * (AUDIO_BITS / 8u));
    wav_cfg.byte_rate = wav_cfg.sample_rate * wav_cfg.block_align;

    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    hw_dfsdm_set_sink(&audio_rb);
    hw_dfsdm_init();

    rec_mgr_init(&rec, &rec_actions);

    usbproto_init(&proto);
    hw_usb_set_rx_line_callback(on_rx_line);

    fs_mount_ok();
    seq = scan_sequence();

    detected = HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT);
    if (detected == GPIO_PIN_RESET)
    {
        open_next_file();
        rec_mgr_event(&rec, REC_EVT_USB_DETACH);
    }
    else
    {
        rec_mgr_event(&rec, REC_EVT_USB_ATTACH);
        rec_mgr_event(&rec, REC_EVT_FINALIZE_DONE);
    }
}

void app_run(void)
{
    static const dfu_stop_hooks_t hooks = {
        dfsdm_stop, finalize_wav, fs_unmount
    };

    usb_level = HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT);
    usb_armed = 0;

    for (;;)
    {
        hw_usb_poll();
        usb_detect_poll();

        switch (rec_mgr_state(&rec))
        {
        case REC_RECORDING:
            if (!file_open)
                open_next_file();
            pump_audio();
            break;

        case REC_STOPPING:
            rec_mgr_event(&rec, REC_EVT_FINALIZE_DONE);
            break;

        default:
            break;
        }

        if (pending_evt == USBPROTO_EVT_DFU)
        {
            pending_evt = USBPROTO_EVT_NONE;
            dfu_enter_with_hooks(&hooks);
        }
    }
}
