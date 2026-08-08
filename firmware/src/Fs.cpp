#include "Fs.h"
#include "ff.h"
#include <string.h>

static FATFS fs;

bool fs_mount(void)
{
    return (f_mount(&fs, "0:", 1) == FR_OK);
}

int fs_mount_result(void)
{
    FRESULT r = f_mount(&fs, "0:", 1);
    return (int)r;
}

void fs_unmount(void)
{
    f_mount(NULL, "0:", 0);
}

/* Write data to a file, then read it back and verify. Returns:
   0 = ok, 1 = mount failed, 2 = open write failed, 3 = write failed,
   4 = close failed, 5 = open read failed, 6 = read mismatch, 7 = read failed */
int fs_test_write(const char* path, const uint8_t* data, size_t n)
{
    FIL f;
    UINT wr = 0, rd = 0;
    uint8_t buf[64];

    if (!fs_mount()) return 1;

    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return 2;
    if (f_write(&f, data, (UINT)n, &wr) != FR_OK || wr != n) { f_close(&f); return 3; }
    if (f_close(&f) != FR_OK) return 4;

    if (f_open(&f, path, FA_READ) != FR_OK) return 5;
    size_t off = 0;
    while (off < n) {
        size_t want = n - off;
        if (want > sizeof(buf)) want = sizeof(buf);
        if (f_read(&f, buf, (UINT)want, &rd) != FR_OK) { f_close(&f); return 7; }
        if (rd != want || memcmp(buf, data + off, rd) != 0) { f_close(&f); return 6; }
        off += rd;
    }
    f_close(&f);
    return 0;
}
