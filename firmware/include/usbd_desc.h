#ifndef USBD_DESC_H
#define USBD_DESC_H

#include "usbd_def.h"

#define DEVICE_ID1 (0x1FFF7A10)
#define DEVICE_ID2 (0x1FFF7A14)
#define DEVICE_ID3 (0x1FFF7A18)
#define USB_SIZ_STRING 32u
#define USBD_VID 0x0483
#define USBD_PID 0x5741
#define USBD_LANGID_STRING 0x0409
#define USBD_MANUFACTURER_STRING "DayVault"
#define USBD_PRODUCT_STRING      "DayVault Recorder"
#define USBD_CONFIGURATION_STRING "DayVault Config"
#define USBD_INTERFACE_STRING    "DayVault CDC"

extern USBD_DescriptorsTypeDef FS_Desc;

#endif
