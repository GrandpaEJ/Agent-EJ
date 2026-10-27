#include "tools_internal.h"
#define _XOPEN_SOURCE 700
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "agent.h"
#include "logger.h"
#include "formats.h"
#include "crypto.h"
int is_path_safe(const char* path) {
    const char* restrict_env = getenv("SANDBOX_RESTRICT");
    if (!restrict_env || strcmp(restrict_env, "1") != 0) {
        return 1;
    }
    char resolved[4096];
    if (!realpath(path, resolved)) {
        char parent[4096];
        snprintf(parent, sizeof(parent), "%s", path);
        char* last_slash = strrchr(parent, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (!realpath(parent, resolved)) {
                return 0;
            }
        } else {
            return 1;
        }
    }
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    size_t cwd_len = strlen(cwd);
    if (strncmp(resolved, cwd, cwd_len) == 0) {
        if (resolved[cwd_len] == '\0' || resolved[cwd_len] == '/') {
            return 1;
        }
    }
    return 0;
}

char* str_tolower(const char* s) {
    char* copy = strdup(s);
    if (!copy) return NULL;
    for (int i = 0; copy[i]; i++) {
        if (copy[i] >= 'A' && copy[i] <= 'Z') {
            copy[i] += 32;
        }
    }
    return copy;
}



int find_skill_file(const char* dir_path, const char* name, char* out_path, size_t out_len) {
    DIR* d = opendir(dir_path);
    if (!d) return 0;
    struct dirent* dir;
    int found_type = 0;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, dir->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                int res = find_skill_file(path, name, out_path, out_len);
                if (res > 0) {
                    found_type = res;
                    break;
                }
            } else {
                char sh_name[256];
                char py_name[256];
                snprintf(sh_name, sizeof(sh_name), "%s.sh", name);
                snprintf(py_name, sizeof(py_name), "%s.py", name);
                if (strcmp(dir->d_name, sh_name) == 0) {
                    if (access(path, X_OK) == 0) {
                        snprintf(out_path, out_len, "%s", path);
                        found_type = 1;
                        break;
                    }
                } else if (strcmp(dir->d_name, py_name) == 0) {
                    if (access(path, F_OK) == 0) {
                        snprintf(out_path, out_len, "%s", path);
                        found_type = 2;
                        break;
                    }
                } else if (strcmp(dir->d_name, name) == 0) {
                    if (access(path, X_OK) == 0) {
                        snprintf(out_path, out_len, "%s", path);
                        found_type = 3;
                        break;
                    }
                }
            }
        }
    }
    closedir(d);
    return found_type;
}


static void load_skills_from_dir(const char* dir_path, cJSON* array) {
    DIR* d = opendir(dir_path);
    if (!d) return;
    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, dir->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                load_skills_from_dir(path, array);
            } else {
                size_t len = strlen(dir->d_name);
                if (len > 5 && strcmp(dir->d_name + len - 5, ".json") == 0) {
                    FILE* fp = fopen(path, "r");
                    if (fp) {
                        fseek(fp, 0, SEEK_END);
                        long sz = ftell(fp);
                        fseek(fp, 0, SEEK_SET);
                        char* buf = malloc(sz + 1);
                        if (buf) {
                            size_t n = fread(buf, 1, sz, fp);
                            buf[n] = '\0';
                            cJSON* skill_json = cJSON_Parse(buf);
                            if (skill_json) {
                                cJSON_AddItemToArray(array, skill_json);
                            }
                            free(buf);
                        }
                        fclose(fp);
                    }
                }
            }
        }
    }
    closedir(d);
}

cJSON* tools_get_definitions(void) {
    const char* schema = "["
        "{\"type\": \"function\", \"function\": {\"name\": \"run_command\", \"description\": \"Execute a shell command. Operating System: Linux. Use this to compile code, search files, run scripts, etc.\", \"parameters\": {\"type\": \"object\", \"properties\": {\"command\": {\"type\": \"string\", \"description\": \"The shell command to run\"}}, \"required\": [\"command\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"read_file\", \"description\": \"Read the contents of a file\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"write_file\", \"description\": \"Write content to a file, overwriting it if it exists\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the file\"}, \"content\": {\"type\": \"string\", \"description\": \"Content to write\"}}, \"required\": [\"path\", \"content\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"list_dir\", \"description\": \"List directory contents\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the directory\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"delete_file\", \"description\": \"Delete a file from the file system\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the file to delete\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"search_file\", \"description\": \"Search for a regex pattern inside files using grep\", \"parameters\": {\"type\": \"object\", \"properties\": {\"pattern\": {\"type\": \"string\", \"description\": \"Regex pattern\"}, \"path\": {\"type\": \"string\", \"description\": \"Directory or file to search\"}}, \"required\": [\"pattern\", \"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"download_file\", \"description\": \"Download a file from a URL to a local path\", \"parameters\": {\"type\": \"object\", \"properties\": {\"url\": {\"type\": \"string\", \"description\": \"The URL to download\"}, \"dest\": {\"type\": \"string\", \"description\": \"The local path to save the file to\"}}, \"required\": [\"url\", \"dest\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"memorize\", \"description\": \"Store a key-value fact in the long-term memory store\", \"parameters\": {\"type\": \"object\", \"properties\": {\"key\": {\"type\": \"string\", \"description\": \"Unique key or topic for this memory\"}, \"value\": {\"type\": \"string\", \"description\": \"The information to store\"}}, \"required\": [\"key\", \"value\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"recall\", \"description\": \"Retrieve information from long-term memory store matching the query\", \"parameters\": {\"type\": \"object\", \"properties\": {\"query\": {\"type\": \"string\", \"description\": \"The search term or keyword\"}}, \"required\": [\"query\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_install\", \"description\": \"Push an APK to an ADB-connected device and install it\", \"parameters\": {\"type\": \"object\", \"properties\": {\"apk_path\": {\"type\": \"string\", \"description\": \"Path to the APK file to install\"}}, \"required\": [\"apk_path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_shell\", \"description\": \"Run a shell command on an ADB-connected device and get its output\", \"parameters\": {\"type\": \"object\", \"properties\": {\"command\": {\"type\": \"string\", \"description\": \"The shell command to run on the device\"}}, \"required\": [\"command\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_devices\", \"description\": \"List all ADB-connected devices\", \"parameters\": {\"type\": \"object\", \"properties\": {}, \"required\": []}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_push\", \"description\": \"Push a local file to an ADB-connected device\", \"parameters\": {\"type\": \"object\", \"properties\": {\"local\": {\"type\": \"string\", \"description\": \"Local file path\"}, \"remote\": {\"type\": \"string\", \"description\": \"Remote destination path on device\"}}, \"required\": [\"local\", \"remote\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_pull\", \"description\": \"Pull a file from an ADB-connected device\", \"parameters\": {\"type\": \"object\", \"properties\": {\"remote\": {\"type\": \"string\", \"description\": \"Remote file path on device\"}, \"local\": {\"type\": \"string\", \"description\": \"Local destination path\"}}, \"required\": [\"remote\", \"local\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_uninstall\", \"description\": \"Uninstall a package from an ADB-connected device\", \"parameters\": {\"type\": \"object\", \"properties\": {\"package\": {\"type\": \"string\", \"description\": \"Package name to uninstall\"}}, \"required\": [\"package\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"read_axml\", \"description\": \"Decode Android Binary XML (AXML) to human-readable XML text\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the binary XML file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"axml_assemble\", \"description\": \"Encode Plaintext XML back to Android Binary XML (AXML)\", \"parameters\": {\"type\": \"object\", \"properties\": {\"src\": {\"type\": \"string\", \"description\": \"Path to the plaintext XML file\"}, \"out\": {\"type\": \"string\", \"description\": \"Path to the output binary XML file\"}}, \"required\": [\"src\", \"out\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"read_dex\", \"description\": \"Parse and dump DEX bytecode file contents or disassemble into a directory\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the DEX file\"}, \"out_dir\": {\"type\": \"string\", \"description\": \"Optional target directory to disassemble all classes into as Smali files\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"decode_apk\", \"description\": \"Fully decompile an APK into a directory, converting DEX files to Smali source\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK file\"}, \"out_dir\": {\"type\": \"string\", \"description\": \"Output directory for decoded files\"}}, \"required\": [\"path\", \"out_dir\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"build_apk\", \"description\": \"Fully rebuild an APK from a decoded directory, automatically assembling Smali files and signing\", \"parameters\": {\"type\": \"object\", \"properties\": {\"src_dir\": {\"type\": \"string\", \"description\": \"Path to the decoded APK directory\"}, \"out_apk\": {\"type\": \"string\", \"description\": \"Output APK file path\"}, \"key_path\": {\"type\": \"string\", \"description\": \"Optional RSA private key path for signing\"}, \"cert_path\": {\"type\": \"string\", \"description\": \"Optional custom certificate path for signing\"}}, \"required\": [\"src_dir\", \"out_apk\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"analyze_apk\", \"description\": \"Analyze an APK file, dumping its manifest, classes, and entries\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"disasm_dex\", \"description\": \"Disassemble a DEX class to Smali\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the DEX file\"}, \"class\": {\"type\": \"number\", \"description\": \"Class index to disassemble (default: 0)\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"smali_assemble\", \"description\": \"Assemble smali directory into a DEX file\", \"parameters\": {\"type\": \"object\", \"properties\": {\"src_dir\": {\"type\": \"string\", \"description\": \"Path to smali source directory\"}, \"out_dex\": {\"type\": \"string\", \"description\": \"Output DEX file path\"}}, \"required\": [\"src_dir\", \"out_dex\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"repack_apk\", \"description\": \"Repack contents of an extracted APK folder back into a ZIP/APK\", \"parameters\": {\"type\": \"object\", \"properties\": {\"dir\": {\"type\": \"string\", \"description\": \"Path to directory with APK contents\"}, \"output\": {\"type\": \"string\", \"description\": \"Output APK path\"}}, \"required\": [\"dir\", \"output\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"resign_apk\", \"description\": \"Resign an APK natively\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK file to sign\"}, \"scheme\": {\"type\": \"string\", \"description\": \"Signing scheme (v1, v2, v3). Default: v1. Example: v1,v2,v3\"}}, \"required\": [\"path\"]}}}"
        "{\"type\": \"function\", \"function\": {\"name\": \"arsc_encode\", \"description\": \"Decode a resource table (APK or resources.arsc), optionally patch values, and encode it back into a resources.arsc\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK or the resources.arsc file\"}, \"out\": {\"type\": \"string\", \"description\": \"Output resources.arsc path\"}, \"set_string\": {\"type\": \"string\", \"description\": \"Comma separated resource id=value pairs to set as strings\"}, \"set_int\": {\"type\": \"string\", \"description\": \"Comma separated resource id=value pairs to set as integers\"}}, \"required\": [\"path\", \"out\"]}}}"
        "{\"type\": \"function\", \"function\": {\"name\": \"arsc_info\", \"description\": \"Summarise the packages, types and configurations of a resource table\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK or the resources.arsc file\"}}, \"required\": [\"path\"]}}}"
        "{\"type\": \"function\", \"function\": {\"name\": \"arsc_compile\", \"description\": \"Compile the res values tree a decode produced back into a resources.arsc, so an edit in res/values XML files reaches the rebuilt APK\", \"parameters\": {\"type\": \"object\", \"properties\": {\"arsc\": {\"type\": \"string\", \"description\": \"Path to the original resources.arsc, or to the APK holding it\"}, \"res_dir\": {\"type\": \"string\", \"description\": \"The res directory written by decode_apk\"}, \"out\": {\"type\": \"string\", \"description\": \"Output resources.arsc path\"}}, \"required\": [\"arsc\", \"res_dir\", \"out\"]}}}"
        "{\"type\": \"function\", \"function\": {\"name\": \"smali_flow\", \"description\": \"Generate Mermaid flowcharts from smali source files. Modes: basic (control flow only), advance (all insns), full (with line numbers). Scopes: method, class, file.\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to .smali file or directory\"}, \"method\": {\"type\": \"string\", \"description\": \"Optional method name filter\"}, \"mode\": {\"type\": \"string\", \"description\": \"Mode: basic|advance|full (default advance)\"}, \"scope\": {\"type\": \"string\", \"description\": \"Scope: method|class|file (default method)\"}}, \"required\": [\"path\"]}}}"
        "]";
    cJSON* array = cJSON_Parse(schema);
    if (!array) return NULL;
    
    load_skills_from_dir("skills", array);
    return array;
}

int check_safety(const char* action) {
    if (!is_cli_mode) return 1;
    const char* safe = getenv("SAFE_MODE");
    if (safe && strcmp(safe, "0") == 0) return 1;
    
    printf("\n\033[1;31m[Safety Check]\033[0m Agent wants to: \033[1;37m%s\033[0m. Allow? [y/N]: ", action);
    fflush(stdout);
    
    char ans[16];
    if (fgets(ans, sizeof(ans), stdin)) {
        if (ans[0] == 'y' || ans[0] == 'Y') return 1;
    }
    return 0;
}




























char* tools_execute(const char* name, cJSON* args) {
    char details[256];
    snprintf(details, sizeof(details), "tool: %s", name);
    log_info("tool_call", details);
    
    if (strcmp(name, "run_command") == 0) return execute_run_command(args);
    if (strcmp(name, "read_file") == 0) return execute_read_file(args);
    if (strcmp(name, "write_file") == 0) return execute_write_file(args);
    if (strcmp(name, "list_dir") == 0) return execute_list_dir(args);
    if (strcmp(name, "delete_file") == 0) return execute_delete_file(args);
    if (strcmp(name, "search_file") == 0) return execute_search_file(args);
    if (strcmp(name, "download_file") == 0) return execute_download_file(args);
    if (strcmp(name, "memorize") == 0) return execute_memorize(args);
    if (strcmp(name, "recall") == 0) return execute_recall(args);
    if (strcmp(name, "adb_install") == 0) return execute_adb_install(args);
    if (strcmp(name, "adb_shell") == 0) return execute_adb_shell(args);
    if (strcmp(name, "adb_devices") == 0) return execute_adb_devices(args);
    if (strcmp(name, "adb_push") == 0) return execute_adb_push(args);
    if (strcmp(name, "adb_pull") == 0) return execute_adb_pull(args);
    if (strcmp(name, "adb_uninstall") == 0) return execute_adb_uninstall(args);
    if (strcmp(name, "read_axml") == 0) return execute_read_axml(args);
    if (strcmp(name, "axml_assemble") == 0) return execute_axml_assemble(args);
    if (strcmp(name, "read_dex") == 0) return execute_read_dex(args);
    char *res = NULL;
    if (strcmp(name, "decode_apk") == 0) res = execute_decode_apk(args);
    else if (strcmp(name, "build_apk") == 0) res = execute_build_apk(args);
    else if (strcmp(name, "analyze_apk") == 0) res = execute_analyze_apk(args);
    if (res) return res;
    if (strcmp(name, "disasm_dex") == 0) return execute_disasm_dex(args);
    if (strcmp(name, "smali_assemble") == 0) return execute_smali_assemble(args);
    if (strcmp(name, "repack_apk") == 0) return execute_repack_apk(args);
    if (strcmp(name, "resign_apk") == 0) return execute_resign_apk(args);
    if (strcmp(name, "zipalign") == 0) return execute_zipalign(args);
    if (strcmp(name, "smali_flow") == 0) return execute_smali_flow(args);
    if (strcmp(name, "arsc_encode") == 0) return execute_arsc_encode(args);
    if (strcmp(name, "arsc_info") == 0) return execute_arsc_info(args);
    if (strcmp(name, "arsc_compile") == 0) return execute_arsc_compile(args);
    
    return execute_dynamic_skill(name, args);
}
