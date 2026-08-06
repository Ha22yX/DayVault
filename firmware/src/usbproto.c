#include "usbproto.h"
#include <string.h>
#include <stdlib.h>

void usbproto_init(usbproto_parser_t *p)
{
    p->len = 0;
    p->line[0] = 0;
}

usbproto_result_t usbproto_feed(usbproto_parser_t *p, uint8_t byte, usbproto_msg_t *out)
{
    if (byte == '\r')
        return USBPROTO_NEED_MORE;
    if (p->len >= USBPROTO_LINE_MAX)
    {
        if (byte == '\n')
        {
            p->len = 0;
            return USBPROTO_UNKNOWN;
        }
        return USBPROTO_NEED_MORE;
    }
    if (byte == '\n')
    {
        p->line[p->len] = 0;
        p->len = 0;
        if (strcmp(p->line, "TIME") == 0)
        {
            out->cmd = USBPROTO_CMD_TIME;
            return USBPROTO_OK;
        }
        if (strcmp(p->line, "STAT") == 0)
        {
            out->cmd = USBPROTO_CMD_STAT;
            return USBPROTO_OK;
        }
        if (strcmp(p->line, "FLUSH") == 0)
        {
            out->cmd = USBPROTO_CMD_FLUSH;
            return USBPROTO_OK;
        }
        if (strncmp(p->line, "SYNC ", 5) == 0)
        {
            out->cmd = USBPROTO_CMD_SYNC;
            out->arg = (uint32_t)strtoul(p->line + 5, 0, 10);
            return USBPROTO_OK;
        }
        return USBPROTO_UNKNOWN;
    }
    if (p->len >= USBPROTO_LINE_MAX - 1)
    {
        p->len = USBPROTO_LINE_MAX;
        return USBPROTO_UNKNOWN;
    }
    p->line[p->len++] = (char)byte;
    return USBPROTO_NEED_MORE;
}
