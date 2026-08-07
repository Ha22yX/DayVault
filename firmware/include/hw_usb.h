#ifndef DAYVAULT_HW_USB_H
#define DAYVAULT_HW_USB_H

#include <stddef.h>
#include <stdint.h>

typedef void (*rx_line_cb)(const char *line, size_t len);

void hw_usb_init(void);
void hw_usb_deinit(void);
void hw_usb_poll(void);
void hw_usb_set_rx_line_callback(rx_line_cb cb);
void cdc_rx_bytes(const uint8_t *data, size_t len);

#endif
