#pragma once

#include <stddef.h>
#include <stdint.h>

bool winusb_start(void);
void winusb_stop(void);
bool winusb_is_configured(void);
bool winusb_tx_busy(void);
bool winusb_send(const uint8_t* data, size_t length);
bool winusb_ack_received(void);
