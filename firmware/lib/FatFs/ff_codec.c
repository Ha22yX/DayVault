#include "ff.h"

/* Code page 850 conversion stubs. DayVault writes machine-generated ASCII
   filenames only; identity conversion keeps the LFN path code working. */
WCHAR ff_convert(WCHAR chr, UINT dir)
{
    (void)dir;
    return chr;
}

WCHAR ff_wtoupper(WCHAR chr)
{
    if (chr >= 'a' && chr <= 'z')
        return (WCHAR)(chr - 0x20);
    if (chr >= 0xE0 && chr <= 0xFE && chr != 0xF7)
        return (WCHAR)(chr - 0x20);
    return chr;
}
