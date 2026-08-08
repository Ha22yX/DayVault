#include "ff.h"
#include "diskio.h"
#include "SdCard.h"

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return sd_init() ? RES_OK : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count)
{
    (void)pdrv;
    return sd_read_sectors(sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count)
{
    (void)pdrv;
    return sd_write_sectors(sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD*)buff = (DWORD)(sd_capacity_bytes() / 512u);
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD*)buff = 512u;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD*)buff = 1u;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)8 << 21) | ((DWORD)8 << 16);
}
