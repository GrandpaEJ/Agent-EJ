#include "agent.h"
#include "http.h"
#include "tools.h"
#include "cJSON.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int chat_id;
    cJSON* history;
} AgentSession;

#define MAX_SESSIONS 64
static AgentSession sessions[MAX_SESSIONS];
static int sessions_count = 0;

void agent_init(void) {
    // Initialized dynamically per session
}

static cJSON* get_session_history(int chat_id) {
    for (int i = 0; i < sessions_count; i++) {
        if (sessions[i].chat_id == chat_id) {
            return sessions[i].history;
        }
    }
    
    if (sessions_count >= MAX_SESSIONS) {
        cJSON_Delete(sessions[0].history);
        for (int i = 1; i < sessions_count; i++) {
            sessions[i-1] = sessions[i];
        }
        sessions_count--;
    }
    
    AgentSession s;
    s.chat_id = chat_id;
    s.history = cJSON_CreateArray();
    
    cJSON* sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", "You are Agent EJ, a helpful AI assistant with system access. Keep answers concise.");
    cJSON_AddItemToArray(s.history, sys_msg);
    
    if (chat_id != 0) {
        char sess_path[1024];
        snprintf(sess_path, sizeof(sess_path), ".telegram_sessions/session_%d.json", chat_id);
        FILE* fp = fopen(sess_path, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char* buf = malloc(sz + 1);
            if (buf) {
                size_t n = fread(buf, 1, sz, fp);
                buf[n] = '\0';
                cJSON* loaded = cJSON_Parse(buf);
                if (loaded && cJSON_IsArray(loaded)) {
                    cJSON_Delete(s.history);
                    s.history = loaded;
                } else if (loaded) {
                    cJSON_Delete(loaded);
                }
                free(buf);
            }
            fclose(fp);
        }
    } else {
        FILE* fp = fopen(".agent_session.json", "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char* buf = malloc(sz + 1);
            if (buf) {
                size_t n = fread(buf, 1, sz, fp);
                buf[n] = '\0';
                cJSON* loaded = cJSON_Parse(buf);
                if (loaded && cJSON_IsArray(loaded)) {
                    cJSON_Delete(s.history);
                    s.history = loaded;
                } else if (loaded) {
                    cJSON_Delete(loaded);
                }
                free(buf);
            }
            fclose(fp);
        }
    }
    
    sessions[sessions_count++] = s;
    return s.history;
}

static void save_session_history(int chat_id, cJSON* history) {
    char* print = cJSON_PrintUnformatted(history);
    if (!print) return;
    
    if (chat_id != 0) {
        char sess_path[1024];
        snprintf(sess_path, sizeof(sess_path), ".telegram_sessions/session_%d.json", chat_id);
        FILE* fp = fopen(sess_path, "w");
        if (fp) {
            fputs(print, fp);
            fclose(fp);
        }
    } else {
        FILE* fp = fopen(".agent_session.json", "w");
        if (fp) {
            fputs(print, fp);
            fclose(fp);
        }
    }
    free(print);
}

static char* recall_memories_for_text(const char* text) {
    FILE* fp = fopen(".agent_memory.json", "r");
    if (!fp) return NULL;
    
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, sz, fp);
    buf[n] = '\0';
    fclose(fp);
    
    cJSON* mem = cJSON_Parse(buf);
    free(buf);
    if (!mem) return NULL;
    
    char* text_copy = strdup(text);
    char* token = strtok(text_copy, " ,.-!?\"';:()[]{}");
    
    size_t out_cap = 1024;
    size_t out_len = 0;
    char* out = malloc(out_cap);
    if (out) out[0] = '\0';
    
    while (token && out) {
        if (strlen(token) >= 4) {
            cJSON* item = mem->child;
            while (item) {
                char* token_lower = malloc(strlen(token) + 1);
                for (int i = 0; token[i]; i++) {
                    token_lower[i] = (token[i] >= 'A' && token[i] <= 'Z') ? token[i] + 32 : token[i];
                }
                token_lower[strlen(token)] = '\0';
                
                char* k_lower = malloc(strlen(item->string) + 1);
                for (int i = 0; item->string[i]; i++) {
                    k_lower[i] = (item->string[i] >= 'A' && item->string[i] <= 'Z') ? item->string[i] + 32 : item->string[i];
                }
                k_lower[strlen(item->string)] = '\0';
                
                char* v_lower = NULL;
                if (item->valuestring) {
                    v_lower = malloc(strlen(item->valuestring) + 1);
                    for (int i = 0; item->valuestring[i]; i++) {
                        v_lower[i] = (item->valuestring[i] >= 'A' && item->valuestring[i] <= 'Z') ? item->valuestring[i] + 32 : item->valuestring[i];
                    }
                    v_lower[strlen(item->valuestring)] = '\0';
                }
                
                int match = 0;
                if (strstr(k_lower, token_lower)) match = 1;
                if (v_lower && strstr(v_lower, token_lower)) match = 1;
                
                free(token_lower);
                free(k_lower);
                if (v_lower) free(v_lower);
                
                if (match) {
                    char entry[1024];
                    snprintf(entry, sizeof(entry), "[%s: %s]", item->string, item->valuestring ? item->valuestring : "");
                    if (!strstr(out, entry)) {
                        if (out_len + strlen(entry) + 3 >= out_cap) {
                            out_cap *= 2;
                            char* new_out = realloc(out, out_cap);
                            if (new_out) out = new_out;
                        }
                        if (out) {
                            if (out_len > 0) {
                                strcat(out, ", ");
                                out_len += 2;
                            }
                            strcat(out, entry);
                            out_len += strlen(entry);
                        }
                    }
                }
                item = item->next;
            }
        }
        token = strtok(NULL, " ,.-!?\"';:()[]{}");
    }
    free(text_copy);
    cJSON_Delete(mem);
    
    if (out && out_len == 0) {
        free(out);
        return NULL;
    }
    return out;
}

static char* make_openai_request(cJSON* history) {
    const char* api_url = getenv("API_BASE_URL");
    if (!api_url || strlen(api_url) == 0) {
        api_url = "https://text.pollinations.ai/openai";
    }
    
    const char* api_model = getenv("API_MODEL");
    if (!api_model || strlen(api_model) == 0) {
        api_model = "openai";
    }
    
    const char* api_key = getenv("API_KEY");
    char auth_header[1024];
    char* auth_ptr = NULL;
    if (api_key && strlen(api_key) > 0) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        auth_ptr = auth_header;
    }

    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", api_model);
    cJSON_AddItemToObject(req, "messages", cJSON_Duplicate(history, 1));
    cJSON_AddItemToObject(req, "tools", tools_get_definitions());
    
    char* json_body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    
    char* response = http_post_json(api_url, auth_ptr, json_body);
    free(json_body);
    return response;
}

char* agent_send_message(const char* user_text, int chat_id) {
    char log_msg[1024];
    snprintf(log_msg, sizeof(log_msg), "chat_id: %d, text: %s", chat_id, user_text);
    log_info("message_received", log_msg);
    
    cJSON* history = get_session_history(chat_id);
    
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", user_text);
    cJSON_AddItemToArray(history, msg);
    
    // Inject dynamic memories based on user keywords
    char* memories = recall_memories_for_text(user_text);
    cJSON* sys_msg = cJSON_GetArrayItem(history, 0);
    if (sys_msg) {
        cJSON* role = cJSON_GetObjectItem(sys_msg, "role");
        if (role && strcmp(role->valuestring, "system") == 0) {
            char sys_content[2048];
            if (memories && strlen(memories) > 0) {
                snprintf(sys_content, sizeof(sys_content), 
                    "You are Agent EJ, a helpful AI assistant with system access. Keep answers concise. "
                    "Relevant recalled memories: %s", memories);
            } else {
                snprintf(sys_content, sizeof(sys_content), 
                    "You are Agent EJ, a helpful AI assistant with system access. Keep answers concise.");
            }
            cJSON_ReplaceItemInObject(sys_msg, "content", cJSON_CreateString(sys_content));
        }
    }
    if (memories) free(memories);
    
    while (1) {
        // 1. Sliding window context management
        while (cJSON_GetArraySize(history) > 20) {
            cJSON_DeleteItemFromArray(history, 1);
        }
        
        // 2. Large tool output truncation for old context (>3 turns ago)
        int hist_size = cJSON_GetArraySize(history);
        for (int i = 1; i < hist_size - 6; i++) {
            cJSON* h_msg = cJSON_GetArrayItem(history, i);
            cJSON* h_role = cJSON_GetObjectItem(h_msg, "role");
            if (h_role && strcmp(h_role->valuestring, "tool") == 0) {
                cJSON* h_content = cJSON_GetObjectItem(h_msg, "content");
                if (h_content && h_content->valuestring && strlen(h_content->valuestring) > 500) {
                    cJSON_ReplaceItemInObject(h_msg, "content", cJSON_CreateString("[Tool output truncated for token efficiency]"));
                }
            }
        }
        
        char* resp_str = NULL;
        for (int retries = 0; retries < 3; retries++) {
            resp_str = make_openai_request(history);
            if (!resp_str) {
                log_warn("network_retry", "Failed to get API response, retrying");
                printf("\033[1;31m[Network Error]\033[0m Retrying (%d/3)...\n", retries+1);
                sleep(2);
                continue;
            }
            cJSON* check = cJSON_Parse(resp_str);
            if (!check) {
                log_warn("api_invalid_json", "Received non-JSON payload from API, retrying");
                printf("\033[1;31m[API Error]\033[0m Invalid JSON, retrying (%d/3)...\n", retries+1);
                free(resp_str);
                resp_str = NULL;
                sleep(2);
                continue;
            }
            cJSON_Delete(check);
            break;
        }
        if (!resp_str) {
            log_error("api_complete_failure", "Failed to retrieve valid API response after 3 attempts");
            return strdup("Error: API completely failed after 3 retries.");
        }
        
        cJSON* resp = cJSON_Parse(resp_str);
        free(resp_str);
        
        cJSON* choices = cJSON_GetObjectItem(resp, "choices");
        if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            cJSON_Delete(resp);
            return strdup("Error: No choices in response");
        }
        
        cJSON* message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
        if (!message) {
            cJSON_Delete(resp);
            return strdup("Error: No message in response");
        }
        
        cJSON_AddItemToArray(history, cJSON_Duplicate(message, 1));
        
        cJSON* tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
            cJSON* tool_call = cJSON_GetArrayItem(tool_calls, 0);
            cJSON* function = cJSON_GetObjectItem(tool_call, "function");
            if (function) {
                cJSON* name_obj = cJSON_GetObjectItem(function, "name");
                cJSON* args_obj = cJSON_GetObjectItem(function, "arguments");
                const char* name = name_obj ? name_obj->valuestring : "";
                const char* args_str = args_obj ? args_obj->valuestring : "{}";
                
                printf("\033[1;33m[Agent Thinking]\033[0m Calling tool '%s' with args: %s\n", name, args_str);
                
                cJSON* args = cJSON_Parse(args_str);
                char* result = tools_execute(name, args);
                cJSON_Delete(args);
                
                cJSON* tool_resp = cJSON_CreateObject();
                cJSON_AddStringToObject(tool_resp, "role", "tool");
                
                cJSON* id_obj = cJSON_GetObjectItem(tool_call, "id");
                if (id_obj) {
                    cJSON_AddStringToObject(tool_resp, "tool_call_id", id_obj->valuestring);
                } else {
                    cJSON_AddStringToObject(tool_resp, "tool_call_id", "call_unknown");
                }
                cJSON_AddStringToObject(tool_resp, "name", name);
                cJSON_AddStringToObject(tool_resp, "content", result);
                
                cJSON_AddItemToArray(history, tool_resp);
                
                free(result);
                cJSON_Delete(resp);
                continue; 
            }
        }
        
        cJSON* content = cJSON_GetObjectItem(message, "content");
        char* final_text = (content && content->valuestring) ? strdup(content->valuestring) : strdup("No text response");
        cJSON_Delete(resp);
        
        // Save current session context to file system
        save_session_history(chat_id, history);
        
        log_info("agent_response", final_text);
        return final_text;
    }
}
