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
            usbproto_init(p);
        return USBPROTO_EVT_NONE;
    }

    if (byte == '\n')
    {
        if (p->len > 0 && p->buf[p->len - 1] == '\r')
            p->len--;
        if (p->len == 3 && memcmp(p->buf, "DFU", 3) == 0)
            evt = USBPROTO_EVT_DFU;
        else if (p->len > 0)
            evt = USBPROTO_EVT_UNKNOWN;
        usbproto_init(p);
        p->last_evt = evt;
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
    usbproto_event_t evt = p->last_evt;
    p->last_evt = USBPROTO_EVT_NONE;
    return evt;
}
