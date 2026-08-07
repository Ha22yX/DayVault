#ifndef DAYVAULT_DFU_H
#define DAYVAULT_DFU_H

typedef struct
{
    void (*stop_acquisition)(void);
    void (*close_segment)(void);
    void (*unmount_storage)(void);
} dfu_stop_hooks_t;

void dfu_enter_with_hooks(const dfu_stop_hooks_t *hooks);

#endif
