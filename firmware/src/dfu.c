#include "dfu.h"
#include "hw_usb.h"
#include "stm32l4xx_hal.h"

#define SYSTEM_MEMORY_BASE 0x1FFF0000u
#define SRAM_LOW_BOUNDARY  0x20000000u

void dfu_enter_with_hooks(const dfu_stop_hooks_t *hooks)
{
    uint32_t msp;

    if (hooks)
    {
        if (hooks->stop_acquisition) hooks->stop_acquisition();
        if (hooks->close_segment)    hooks->close_segment();
        if (hooks->unmount_storage)  hooks->unmount_storage();
    }

    HAL_Delay(20);

    /* Tear down USB before jumping so the ROM bootloader can re-enumerate. */
    hw_usb_deinit();
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    __disable_irq();

    msp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
    if ((msp & 0xFFF00000u) != SRAM_LOW_BOUNDARY)
        while (1) { }   /* invalid bootloader stack — do not jump */

    __set_MSP(msp);
    ((void (*)(void)) * (volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4u))();
}
