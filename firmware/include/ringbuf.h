#ifndef DAYVAULT_RINGBUF_H
#define DAYVAULT_RINGBUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *buf;
    size_t size;
    size_t head;
    size_t tail;
    size_t used;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size);
size_t ringbuf_used(const ringbuf_t *rb);
size_t ringbuf_free(const ringbuf_t *rb);
size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t n);
size_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, size_t n);

#endif
