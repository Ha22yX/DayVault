#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool fs_mount(void);
int  fs_mount_result(void);
void fs_unmount(void);
int  fs_test_write(const char* path, const uint8_t* data, size_t n);

#ifdef __cplusplus
}
#endif
