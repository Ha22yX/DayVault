#include "usbd_msc_storage.h"
#include "hw_spi_sd.h"
#include <string.h>

static int8_t usbd_msc_storage_inquiry[36] =
{
    0x00, 0x80, 0x02, 0x02, 31, 0x00, 0x00, 0x00,
    'D', 'a', 'y', 'V', 'a', 'u', 'l', 't',
    'D', 'a', 'y', 'V', 'a', 'u', 'l', 't',
    ' ', 'S', 't', 'o', 'r', 'a', 'g', 'e',
    '1', '.', '0', '0'
};

static int8_t STORAGE_Init(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;
    *block_size = 512u;
    *block_num = (uint32_t)(hw_sd_capacity_bytes() / 512u);
    return 0;
}

static int8_t STORAGE_IsReady(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
    (void)lun;
    return 1;   /* read-only */
}

static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return hw_sd_read_sectors(blk_addr, buf, blk_len, 1) ? 0 : -1;
}

static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun; (void)buf; (void)blk_addr; (void)blk_len;
    return -1;   /* write protected */
}

static int8_t STORAGE_GetMaxLun(void)
{
    return 0;
}

USBD_StorageTypeDef usbd_msc_storage_fops =
{
    STORAGE_Init,
    STORAGE_GetCapacity,
    STORAGE_IsReady,
    STORAGE_IsWriteProtected,
    STORAGE_Read,
    STORAGE_Write,
    STORAGE_GetMaxLun,
    usbd_msc_storage_inquiry
};
