#include "telegram.h"
#include "agent.h"
#include "http.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* bot_token = NULL;
static const char* allowed_user_ids = NULL;

static int is_user_allowed(int user_id) {
    if (!allowed_user_ids) return 0;
    
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%d", user_id);
    
    char* copy = strdup(allowed_user_ids);
    char* token = strtok(copy, ",");
    int allowed = 0;
    while (token) {
        if (strcmp(token, uid_str) == 0) {
            allowed = 1;
            break;
        }
        token = strtok(NULL, ",");
    }
    free(copy);
    return allowed;
}

static void send_telegram_message(int chat_id, const char* text) {
    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", bot_token);
    
    cJSON* req = cJSON_CreateObject();
    cJSON_AddNumberToObject(req, "chat_id", chat_id);
    cJSON_AddStringToObject(req, "text", text);
    
    char* json_body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    
    char* resp = http_post_json(url, NULL, json_body);
    free(json_body);
    free(resp);
}

void agent_run_telegram(void) {
    bot_token = getenv("TELEGRAM_BOT_TOKEN");
    if (!bot_token) {
        fprintf(stderr, "Error: TELEGRAM_BOT_TOKEN not set\n");
        return;
    }
    allowed_user_ids = getenv("ALLOWED_TELEGRAM_USER_IDS");
    if (!allowed_user_ids) {
        fprintf(stderr, "Error: ALLOWED_TELEGRAM_USER_IDS not set\n");
        return;
    }
    
    printf("Agent EJ Telegram Mode started...\n");
    
    long last_update_id = 0;
    
    while (1) {
        char url[512];
        snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getUpdates?offset=%ld&timeout=10", bot_token, last_update_id + 1);
        
        char* resp_str = http_get(url, NULL);
        if (!resp_str) {
            sleep(2);
            continue;
        }
        
        cJSON* resp = cJSON_Parse(resp_str);
        free(resp_str);
        if (!resp) {
            sleep(2);
            continue;
        }
        
        cJSON* ok = cJSON_GetObjectItem(resp, "ok");
        if (cJSON_IsTrue(ok)) {
            cJSON* result = cJSON_GetObjectItem(resp, "result");
            int i;
            for (i = 0; i < cJSON_GetArraySize(result); i++) {
                cJSON* update = cJSON_GetArrayItem(result, i);
                cJSON* update_id = cJSON_GetObjectItem(update, "update_id");
                if (update_id->valueint > last_update_id) {
                    last_update_id = update_id->valueint;
                }
                
                cJSON* message = cJSON_GetObjectItem(update, "message");
                if (message) {
                    cJSON* from = cJSON_GetObjectItem(message, "from");
                    cJSON* chat = cJSON_GetObjectItem(message, "chat");
                    cJSON* text = cJSON_GetObjectItem(message, "text");
                    
                    if (from && chat && text) {
                        int user_id = cJSON_GetObjectItem(from, "id")->valueint;
                        int chat_id = cJSON_GetObjectItem(chat, "id")->valueint;
                        
                        if (is_user_allowed(user_id)) {
                            char* agent_resp = agent_send_message(text->valuestring, chat_id);
                            send_telegram_message(chat_id, agent_resp);
                            free(agent_resp);
                        } else {
                            printf("Unauthorized user: %d\n", user_id);
                        }
                    }
                }
            }
        }
        
        cJSON_Delete(resp);
    }
}
