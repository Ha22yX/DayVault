#ifndef DAYVAULT_USBPROTO_H
#define DAYVAULT_USBPROTO_H

#include <stdint.h>
#include <stddef.h>

#define USBPROTO_LINE_MAX 64u

typedef enum
{
    USBPROTO_CMD_SYNC,
    USBPROTO_CMD_TIME,
    USBPROTO_CMD_STAT,
    USBPROTO_CMD_FLUSH
} usbproto_cmd_t;

typedef enum
{
    USBPROTO_OK,
    USBPROTO_NEED_MORE,
    USBPROTO_UNKNOWN
} usbproto_result_t;

typedef struct
{
    usbproto_cmd_t cmd;
    uint32_t arg;   /* unix seconds for SYNC */
} usbproto_msg_t;

typedef struct
{
    char line[USBPROTO_LINE_MAX];
    uint8_t len;
} usbproto_parser_t;

void usbproto_init(usbproto_parser_t *p);
usbproto_result_t usbproto_feed(usbproto_parser_t *p, uint8_t byte, usbproto_msg_t *out);

#endif
