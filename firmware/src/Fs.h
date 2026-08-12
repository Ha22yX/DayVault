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
uint32_t fs_next_sequence(void);
int  fs_test_write(const char* path, const uint8_t* data, size_t n);
uint64_t fs_free_bytes(void);
int  fs_make_space(uint64_t want_free, const char* skip_basename);
int  fs_delete_oldest(const char* skip_basename);
int  fs_delete_recording(const char* basename, const char* active_basename);

#ifdef __cplusplus
}
#endif
