#include "app.h"
#include "usbproto.h"
#include "hw_usb.h"
#include "dfu.h"

static usbproto_t proto;
static volatile usbproto_event_t pending_evt = USBPROTO_EVT_NONE;

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

static void noop(void)
{
}

void app_init(void)
{
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
