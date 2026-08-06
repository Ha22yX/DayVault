#ifndef DAYVAULT_HW_GPIO_H
#define DAYVAULT_HW_GPIO_H

#include <stdint.h>

void hw_gpio_init(void);
uint8_t hw_gpio_usb_detect(void);
int hw_gpio_usb_event_pending(void);
void hw_gpio_clear_usb_event(void);

#endif
