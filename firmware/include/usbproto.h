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
    usbproto_event_t last_evt;
} usbproto_t;

void usbproto_init(usbproto_t *p);
usbproto_event_t usbproto_feed(usbproto_t *p, uint8_t byte);
usbproto_event_t usbproto_poll(usbproto_t *p);

#endif
