#include "Fs.h"
#include "ff.h"
#include "Config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static FATFS fs;

bool fs_mount(void)
{
    if (fs.fs_type != 0) return true;              /* already mounted -> never re-mount */
    return (f_mount(&fs, "0:", 1) == FR_OK);
}

int fs_mount_result(void)
{
    if (fs.fs_type != 0) return (int)FR_OK;        /* already mounted -> never re-mount */
    FRESULT r = f_mount(&fs, "0:", 1);
    return (int)r;
}

void fs_unmount(void)
{
    f_mount(NULL, "0:", 0);
}

static uint32_t parse_num(const char* s)
{
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10u + (uint32_t)(*s - '0');
        if (n > REC_SEQ_MAX) n = REC_SEQ_MAX;
        s++;
    }
    return n;
}

static bool extension_equals(const char* extension, const char* expected)
{
    if (extension == NULL || expected == NULL) return false;
    while (*extension != '\0' && *expected != '\0') {
        char a = *extension++;
        char b = *expected++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return *extension == '\0' && *expected == '\0';
}

static bool is_recording_extension(const char* extension)
{
    return extension_equals(extension, REC_EXT_STR) || extension_equals(extension, "WAV");
}

uint32_t fs_next_sequence(void)
{
    DIR dir;
    FILINFO fno;
    uint32_t max_num = 0;
    if (f_opendir(&dir, "0:/") != FR_OK) return 1;
    for (;;) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        if ((fno.fattrib & AM_DIR) != 0) continue;
        if (strncmp(fno.fname, REC_DIR_STR, strlen(REC_DIR_STR)) != 0) continue;
        char* dot = strrchr(fno.fname, '.');
        if (dot == NULL || !extension_equals(dot + 1, REC_EXT_STR)) continue;
        uint32_t n = parse_num(fno.fname + strlen(REC_DIR_STR));
        if (n > max_num) max_num = n;
    }
    f_closedir(&dir);
    return (max_num >= REC_SEQ_MAX) ? 1 : max_num + 1;
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

typedef struct { char name[40]; uint32_t size; } rec_file_t;

static bool is_seq_name(const char* n) { return (n[3] != '-'); }   /* "REC###..." vs "REC-..." */
static uint32_t seq_num(const char* n)
{
    uint32_t v = 0;
    const char* p = n + 3;
    while (*p >= '0' && *p <= '9') { v = v * 10u + (uint32_t)(*p - '0'); p++; }
    return v;
}

static int rec_oldest_cmp(const void* a, const void* b)
{
    const rec_file_t* A = (const rec_file_t*)a;
    const rec_file_t* B = (const rec_file_t*)b;
    bool sa = is_seq_name(A->name), sb = is_seq_name(B->name);
    if (sa != sb) return sa ? -1 : 1;                    /* seq files are the old era -> oldest */
    if (sa) {
        uint32_t na = seq_num(A->name), nb = seq_num(B->name);
        return (na < nb) ? -1 : (na > nb) ? 1 : 0;
    }
    return strcmp(A->name, B->name);                     /* timestamp names sort chronologically */
}

uint64_t fs_free_bytes(void)
{
    FATFS* fatfs;
    DWORD nclst = 0;
    if (f_getfree("0:", &nclst, &fatfs) != FR_OK) return 0;
    return (uint64_t)nclst * fatfs->csize * _MIN_SS;
}

static int fs_collect_rec(rec_file_t* arr, int cap, const char* skip)
{
    DIR dir;
    FILINFO fno;
    int n = 0;
    if (f_opendir(&dir, "0:/") != FR_OK) return 0;
    while (n < cap && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if ((fno.fattrib & AM_DIR) != 0) continue;
        if (strncmp(fno.fname, REC_DIR_STR, strlen(REC_DIR_STR)) != 0) continue;
        char* dot = strrchr(fno.fname, '.');
        if (dot == NULL || !is_recording_extension(dot + 1)) continue;
        if (skip != NULL && strcmp(fno.fname, skip) == 0) continue;   /* never touch active recording */
        strncpy(arr[n].name, fno.fname, sizeof(arr[n].name) - 1);
        arr[n].name[sizeof(arr[n].name) - 1] = 0;
        arr[n].size = (uint32_t)fno.fsize;
        n++;
    }
    f_closedir(&dir);
    return n;
}

static int fs_delete_oldest_candidates(const char* skip, int limit)
{
    rec_file_t arr[64];
    int n = fs_collect_rec(arr, 64, skip);
    if (n == 0) return 0;
    qsort(arr, (size_t)n, sizeof(rec_file_t), rec_oldest_cmp);
    int deleted = 0;
    for (int i = 0; i < n && deleted < limit; i++) {
        char path[44];
        snprintf(path, sizeof(path), "0:/%s", arr[i].name);
        if (f_unlink(path) == FR_OK) deleted++;
    }
    return deleted;
}

static bool is_recording_basename(const char* name)
{
    if (name == NULL || strncmp(name, REC_DIR_STR, strlen(REC_DIR_STR)) != 0) {
        return false;
    }
    const size_t length = strlen(name);
    if (length == 0 || length >= sizeof(((rec_file_t*)0)->name)) return false;
    for (size_t i = 0; i < length; i++) {
        const char c = name[i];
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!safe) return false;
    }
    const char* dot = strrchr(name, '.');
    return dot != NULL && is_recording_extension(dot + 1);
}

int fs_delete_recording(const char* basename, const char* active_basename)
{
    if (!is_recording_basename(basename)) return (int)FR_INVALID_NAME;
    if (active_basename != NULL && strcmp(basename, active_basename) == 0) {
        return (int)FR_DENIED;
    }
    if (!fs_mount()) return (int)FR_NOT_READY;
    char path[44];
    snprintf(path, sizeof(path), "0:/%s", basename);
    return (int)f_unlink(path);
}

int fs_delete_oldest(const char* skip) { return fs_delete_oldest_candidates(skip, 1); }

int fs_make_space(uint64_t want_free, const char* skip)
{
    if (fs_free_bytes() >= want_free) return 0;
    int deleted = 0;
    while (fs_free_bytes() < want_free) {
        int d = fs_delete_oldest_candidates(skip, 1);
        if (d == 0) break;
        deleted += d;
    }
    return deleted;
}
