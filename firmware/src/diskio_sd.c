#include "diskio.h"
#include "ff_gen_drv.h"
#include "hw_spi_sd.h"

static DSTATUS sd_initialize(BYTE pdrv)
{
    (void)pdrv;
    return hw_sd_init() ? RES_OK : STA_NOINIT;
}

static DSTATUS sd_status(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

static DRESULT sd_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    if (hw_sd_read_sectors(sector, buff, count))
        return RES_OK;
    return RES_ERROR;
}

static DRESULT sd_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    if (hw_sd_write_sectors(sector, buff, count))
        return RES_OK;
    return RES_ERROR;
}

static DRESULT sd_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = (DWORD)(hw_sd_capacity_bytes() / 512u);
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512u;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1u;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

const Diskio_drvTypeDef Diskio_SD_Driver =
{
    sd_initialize,
    sd_status,
    sd_read,
    sd_write,
    sd_ioctl
};
