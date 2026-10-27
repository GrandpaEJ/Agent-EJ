#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

static void write_log(const char* level, const char* event, const char* details) {
    FILE* fp = fopen("agent-ej.log", "a");
    if (!fp) return;
    
    time_t rawtime;
    struct tm* timeinfo;
    char time_str[64];
    
    time(&rawtime);
    timeinfo = gmtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
    
    // Simple escape for quotes in details
    char escaped_details[1024] = {0};
    if (details) {
        int d_idx = 0;
        for (int i = 0; details[i] && d_idx < (int)sizeof(escaped_details) - 2; i++) {
            if (details[i] == '"' || details[i] == '\\') {
                escaped_details[d_idx++] = '\\';
            }
            escaped_details[d_idx++] = details[i];
        }
    }
    
    fprintf(fp, "{\"time\":\"%s\",\"level\":\"%s\",\"event\":\"%s\",\"details\":\"%s\"}\n", 
            time_str, level, event, escaped_details);
    fclose(fp);
}

void log_info(const char* event, const char* details) {
    write_log("INFO", event, details);
}
void log_warn(const char* event, const char* details) {
    write_log("WARN", event, details);
}
void log_error(const char* event, const char* details) {
    write_log("ERROR", event, details);
}
