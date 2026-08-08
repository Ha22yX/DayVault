#pragma once
#include <stdint.h>
void nr_init(void);
void nr_process(const int16_t* in, int16_t* out, int n);
