#ifndef ZACLR_PROCESS_TABLE_H
#define ZACLR_PROCESS_TABLE_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZACLR_PROCESS_TABLE_MAX_ENTRIES  16u
#define ZACLR_PROCESS_IMAGE_NAME_MAX    64u

struct zaclr_process_entry {
    zaclr_process_id pid;
    zaclr_process_id ppid;
    zaclr_app_domain_id domain_id;
    enum zaclr_process_state state;
    zaclr_user_id uid;
    zaclr_group_id gid;
    int32_t exit_code;
    uint32_t flags;
    char image_name[ZACLR_PROCESS_IMAGE_NAME_MAX];
};

struct zaclr_process_table {
    struct zaclr_process_entry entries[ZACLR_PROCESS_TABLE_MAX_ENTRIES];
    uint32_t count;
};

struct zaclr_result zaclr_process_table_initialize(struct zaclr_process_table* table);

struct zaclr_result zaclr_process_table_register(struct zaclr_process_table* table,
                                                  zaclr_process_id pid,
                                                  zaclr_process_id ppid,
                                                  zaclr_app_domain_id domain_id,
                                                  zaclr_user_id uid,
                                                  zaclr_group_id gid,
                                                  const char* image_name);

struct zaclr_result zaclr_process_table_set_state(struct zaclr_process_table* table,
                                                   zaclr_process_id pid,
                                                   enum zaclr_process_state state);

struct zaclr_result zaclr_process_table_set_exit_code(struct zaclr_process_table* table,
                                                       zaclr_process_id pid,
                                                       int32_t exit_code);

const struct zaclr_process_entry* zaclr_process_table_find(const struct zaclr_process_table* table,
                                                            zaclr_process_id pid);

uint32_t zaclr_process_table_count(const struct zaclr_process_table* table);

const char* zaclr_process_state_name(enum zaclr_process_state state);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PROCESS_TABLE_H */
