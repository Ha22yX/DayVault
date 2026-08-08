#include "RingBuf.h"

#if defined(__arm__)
static inline unsigned long irq_mask_save(void)
{
    unsigned long primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");
    return primask;
}
static inline void irq_mask_restore(unsigned long primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}
#else
static inline unsigned long irq_mask_save(void) { return 0; }
static inline void irq_mask_restore(unsigned long pm) { (void)pm; }
#endif

void ringbuf_init(RingBuf* rb, uint8_t* buf, size_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
}

size_t ringbuf_write(RingBuf* rb, const uint8_t* data, size_t n)
{
    size_t i;
    unsigned long pm = irq_mask_save();
    for (i = 0; i < n; i++) {
        if (rb->used == rb->size) break;
        rb->buf[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
        rb->used++;
    }
    irq_mask_restore(pm);
    return i;
}

size_t ringbuf_read(RingBuf* rb, uint8_t* dst, size_t n)
{
    size_t i;
    unsigned long pm = irq_mask_save();
    for (i = 0; i < n && rb->used > 0; i++) {
        dst[i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        rb->used--;
    }
    irq_mask_restore(pm);
    return i;
}

size_t ringbuf_used(const RingBuf* rb)
{
    return rb->used;
}
