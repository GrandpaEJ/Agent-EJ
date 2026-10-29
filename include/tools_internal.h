#ifndef TOOLS_INTERNAL_H
#define TOOLS_INTERNAL_H

#include "cJSON.h"

char* execute_memorize(cJSON* args);
char* execute_recall(cJSON* args);
char* execute_read_file(cJSON* args);
char* execute_write_file(cJSON* args);
char* execute_list_dir(cJSON* args);
char* execute_delete_file(cJSON* args);
char* execute_search_file(cJSON* args);
char* execute_dynamic_skill(const char* name, cJSON* args);
char* execute_run_command(cJSON* args);
char* execute_download_file(cJSON* args);
char* execute_adb_install(cJSON* args);
char* execute_adb_shell(cJSON* args);
char* execute_adb_devices(cJSON* args);
char* execute_adb_push(cJSON* args);
char* execute_adb_pull(cJSON* args);
char* execute_adb_uninstall(cJSON* args);
char* execute_read_axml(cJSON* args);
char* execute_axml_assemble(cJSON* args);
char* execute_decode_apk(cJSON* args);
char* execute_build_apk(cJSON* args);
char* execute_merge_apk(cJSON* args);
char* execute_deobfuscate(cJSON* args);
char* execute_analyze_apk(cJSON* args);
char* execute_disasm_dex(cJSON* args);
char* execute_smali_assemble(cJSON* args);
char* execute_repack_apk(cJSON* args);
char* execute_resign_apk(cJSON* args);
char* execute_read_dex(cJSON* args);
char* execute_smali_flow(cJSON* args);
char* execute_zipalign(cJSON* args);
char* execute_arsc_encode(cJSON* args);
char* execute_arsc_info(cJSON* args);
char* execute_arsc_compile(cJSON* args);
char* execute_sigkill(cJSON* args);

// Helpers
int resolve_default_key_path(char *out, size_t cap);
int find_skill_file(const char* dir_path, const char* name, char* out_path, size_t out_len);
int is_path_safe(const char* path);
char* str_tolower(const char* s);
int check_safety(const char* action);
char* execute_run_command_from(const char *cmd);

#endif
