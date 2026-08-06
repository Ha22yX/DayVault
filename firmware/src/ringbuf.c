#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

size_t ringbuf_used(const ringbuf_t *rb)
{
    return rb->count;
}

size_t ringbuf_free(const ringbuf_t *rb)
{
    return rb->size - rb->count;
}

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t n)
{
    size_t i;
    for (i = 0; i < n && rb->count < rb->size; i++)
    {
        rb->buf[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
        rb->count++;
    }
    return i;
}

size_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n && rb->count > 0; i++)
    {
        dst[i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        rb->count--;
    }
    return i;
}
