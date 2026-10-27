#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent.h"
#include "logger.h"
#include "tools.h"
#include "cJSON.h"
#ifndef PICO_MODE
#include "telegram.h"
#include "linenoise.h"
#endif

#ifdef PICO_MODE
void agent_run_cli(void) {
    char line[4096];
    printf("\033[1;36mAgent EJ Pico Mode\033[0m (Type 'exit' to quit)\n");
    while (1) {
        printf("\033[1;32m>\033[0m ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = 0;
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
        if (strlen(line) > 0) {
            char* resp = agent_send_message(line, 0);
            printf("\033[1;37m%s\033[0m\n", resp);
            free(resp);
        }
    }
}
#else
void agent_run_cli(void) {
    char* line;
    linenoiseHistorySetMaxLen(100);
    printf("\033[1;36mAgent EJ CLI Mode\033[0m (Type 'exit' to quit)\n");
    while ((line = linenoise("\033[1;32m>\033[0m ")) != NULL) {
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            char* resp = agent_send_message(line, 0);
            printf("\033[1;37m%s\033[0m\n", resp);
            free(resp);
        }
        free(line);
    }
}
#endif

int is_cli_mode = 0;

static void load_env(void) {
    FILE* fp = fopen(".env", "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            char* key = line;
            char* val = eq + 1;
            val[strcspn(val, "\r\n")] = 0;
            setenv(key, val, 1);
        }
    }
    fclose(fp);
}

#ifndef PICO_MODE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);
    
    if (setsid() < 0) exit(1);
    
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);
    
    umask(0);
    
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
    
    FILE* fp = fopen("agent-ej.pid", "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
    log_info("daemon_started", "Agent EJ background daemon successfully started");
}
#endif

int main(int argc, char** argv) {
    load_env();
    
#ifdef PICO_MODE
    (void)argc;
    (void)argv;
    is_cli_mode = 1;
    agent_init();
    log_info("cli_started", "Pico mode CLI started");
    agent_run_cli();
#else
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <cli|telegram|tool|tools> [--daemon]\n", argv[0]);
        fprintf(stderr, "  cli        Interactive AI agent CLI\n");
        fprintf(stderr, "  telegram   Telegram bot mode\n");
        fprintf(stderr, "  tool       Direct tool execution (no AI): %s tool <name> [key=val ...]\n", argv[0]);
        fprintf(stderr, "  tools      List available tools\n");
        return 1;
    }
    
    int run_daemon = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0) {
            run_daemon = 1;
        }
    }
    
    agent_init();
    
    if (strcmp(argv[1], "cli") == 0) {
        is_cli_mode = 1;
        log_info("cli_started", "CLI mode started");
        agent_run_cli();
    } else if (strcmp(argv[1], "telegram") == 0) {
        if (run_daemon) {
            daemonize();
        } else {
            log_info("telegram_started", "Telegram bot started (foreground)");
        }
        agent_run_telegram();
    } else if (strcmp(argv[1], "tool") == 0 && argc >= 3) {
        is_cli_mode = 1;
        agent_init();
        cJSON* args = cJSON_CreateObject();
        for (int i = 3; i < argc; i++) {
            char* eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                char *val = eq + 1;
                char *end;
                long n = strtol(val, &end, 0);
                if (*end == '\0')
                    cJSON_AddNumberToObject(args, argv[i], (double)n);
                else
                    cJSON_AddStringToObject(args, argv[i], val);
                *eq = '=';
            }
        }
        char* result = tools_execute(argv[2], args);
        cJSON_Delete(args);
        printf("%s\n", result ? result : "null");
        /* Direct tool execution reports failure through a numeric JSON
         * "status" field (e.g. smali_assemble); surface it as the process
         * exit code so shell callers can detect it. */
        int rc = 0;
        cJSON* parsed = result ? cJSON_Parse(result) : NULL;
        if (parsed) {
            cJSON* status = cJSON_GetObjectItem(parsed, "status");
            if (cJSON_IsNumber(status) && cJSON_GetNumberValue(status) != 0) rc = 1;
            cJSON_Delete(parsed);
        }
        free(result);
        return rc;
    } else if (strcmp(argv[1], "tools") == 0) {
        cJSON* defs = tools_get_definitions();
        if (defs) {
            char* print = cJSON_Print(defs);
            printf("%s\n", print);
            free(print);
            cJSON_Delete(defs);
        }
    } else {
        fprintf(stderr, "Usage: %s <cli|telegram|tool|tools> [--daemon]\n", argv[0]);
        fprintf(stderr, "  cli        Interactive AI agent CLI\n");
        fprintf(stderr, "  telegram   Telegram bot mode\n");
        fprintf(stderr, "  tool       Direct tool execution (no AI): %s tool <name> [key=val ...]\n", argv[0]);
        fprintf(stderr, "  tools      List available tools\n");
        return 1;
    }
#endif
    
    return 0;
}
