#ifndef USBD_DESC_H
#define USBD_DESC_H

#include "usbd_def.h"

#define DEVICE_ID1    (0x1FFF7590U)
#define DEVICE_ID2    (0x1FFF7594U)
#define DEVICE_ID3    (0x1FFF7598U)

#define USB_SIZ_STRING 64U

extern USBD_DescriptorsTypeDef DayVault_Desc;

#endif
