#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* buf;
    size_t size;
    volatile size_t head;
    volatile size_t tail;
    volatile size_t used;
} RingBuf;

void  ringbuf_init(RingBuf* rb, uint8_t* buf, size_t size);
size_t ringbuf_write(RingBuf* rb, const uint8_t* data, size_t n);
size_t ringbuf_read(RingBuf* rb, uint8_t* dst, size_t n);
size_t ringbuf_used(const RingBuf* rb);

#ifdef __cplusplus
}
#endif
