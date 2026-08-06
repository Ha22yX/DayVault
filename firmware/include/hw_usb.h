#ifndef DAYVAULT_HW_USB_H
#define DAYVAULT_HW_USB_H

#include "usbproto.h"
#include <stdint.h>

void hw_usb_init(void);
void hw_usb_deinit(void);
void hw_usb_poll(void);
int hw_usb_cdc_send(const uint8_t *buf, uint16_t len);
void hw_usb_handle_command(const usbproto_msg_t *msg);

#endif
