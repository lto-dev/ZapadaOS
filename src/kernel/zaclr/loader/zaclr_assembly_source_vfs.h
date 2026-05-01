#ifndef ZACLR_ASSEMBLY_SOURCE_VFS_H
#define ZACLR_ASSEMBLY_SOURCE_VFS_H

#include <kernel/zaclr/loader/zaclr_assembly_source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_assembly_source* zaclr_assembly_source_vfs(void);
struct zaclr_result zaclr_assembly_source_vfs_configure(const char* root_path);
struct zaclr_result zaclr_assembly_source_vfs_publish(const char* path,
                                                      const uint8_t* data,
                                                      size_t size);
struct zaclr_result zaclr_assembly_source_vfs_publish_begin(const char* path,
                                                            size_t size);
struct zaclr_result zaclr_assembly_source_vfs_publish_append(const char* path,
                                                             const uint8_t* data,
                                                             size_t size);
struct zaclr_result zaclr_assembly_source_vfs_publish_end(const char* path);
const char* zaclr_assembly_source_vfs_root(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ASSEMBLY_SOURCE_VFS_H */
