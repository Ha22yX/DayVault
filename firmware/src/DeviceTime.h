#pragma once
#include <stdint.h>
#include <stddef.h>
void dt_init(void);
void dt_set_unix(uint32_t unix);
uint32_t dt_get_unix(void);
void dt_format(char* buf, size_t len);
