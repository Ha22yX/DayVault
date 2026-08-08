#include "UsbMscStorage.h"
#include "SdCard.h"

extern "C" {

static int8_t USBD_STORAGE_Init(uint8_t lun)
{
    (void)lun;
    return USBD_OK;
}

static int8_t USBD_STORAGE_GetCapacity(uint8_t lun, uint32_t* block_num, uint16_t* block_size)
{
    (void)lun;
    uint64_t bytes = sd_capacity_bytes();
    if (bytes == 0)
    {
        return USBD_FAIL;
    }
    *block_size = 512u;
    *block_num = (uint32_t)(bytes / 512u);
    return USBD_OK;
}

static int8_t USBD_STORAGE_IsReady(uint8_t lun)
{
    (void)lun;
    return (sd_capacity_bytes() != 0) ? USBD_OK : USBD_FAIL;
}

static int8_t USBD_STORAGE_IsWriteProtected(uint8_t lun)
{
    (void)lun;
    return USBD_FAIL;
}

static int8_t USBD_STORAGE_Read(uint8_t lun, uint8_t* buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    if (sd_read_sectors(blk_addr, buf, blk_len))
    {
        return USBD_OK;
    }
    return USBD_FAIL;
}

static int8_t USBD_STORAGE_Write(uint8_t lun, uint8_t* buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    (void)buf;
    (void)blk_addr;
    (void)blk_len;
    return USBD_FAIL;
}

static int8_t USBD_STORAGE_GetMaxLun(void)
{
    return 0;
}

static int8_t USBD_MSC_Storage_InquiryData[] =
{
    (int8_t)0x00,
    (int8_t)0x80,
    (int8_t)0x02,
    (int8_t)0x02,
    (int8_t)(STANDARD_INQUIRY_DATA_LEN - 5),
    (int8_t)0x00,
    (int8_t)0x00,
    (int8_t)0x00,
    'D', 'a', 'y', 'V', 'a', 'u', 'l', 't',
    'M', 'S', 'C', ' ', 'D', 'i', 's', 'k',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    '0', '.', '0', '1',
};

USBD_StorageTypeDef USBD_MSC_Storage_fops =
{
    USBD_STORAGE_Init,
    USBD_STORAGE_GetCapacity,
    USBD_STORAGE_IsReady,
    USBD_STORAGE_IsWriteProtected,
    USBD_STORAGE_Read,
    USBD_STORAGE_Write,
    USBD_STORAGE_GetMaxLun,
    USBD_MSC_Storage_InquiryData,
};

}
