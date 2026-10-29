CC = zig cc -target x86_64-linux-musl
OPT = -O0 -g
WARN = -Wall -Wextra
STD = -std=c11 -D_POSIX_C_SOURCE=200809L
INC = -Iinclude -Isrc/android/smali -Isrc/android/arsc -Ivendor/cJSON -Ivendor/linenoise
LTO = -flto -ffunction-sections -fdata-sections

CFLAGS = $(OPT) $(WARN) $(STD) $(INC) $(LTO)
LDFLAGS = $(LTO)

SRC_DIRS = src src/core src/net src/tools src/formats src/formats/zip src/android src/android/smali src/android/adb src/android/axml src/android/dex src/android/apk src/android/arsc src/android/sigkill src/crypto
VENDOR_DIR = vendor
OBJ_DIR = obj

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
CJSON_SRCS = $(wildcard $(VENDOR_DIR)/cJSON/*.c)
LINENOISE_SRCS = $(wildcard $(VENDOR_DIR)/linenoise/*.c)

OBJS = $(patsubst src/%,$(OBJ_DIR)/%,$(SRCS:.c=.o))
CJSON_OBJS = $(patsubst $(VENDOR_DIR)/cJSON/%.c,$(OBJ_DIR)/%.o,$(CJSON_SRCS))
LINENOISE_OBJS = $(patsubst $(VENDOR_DIR)/linenoise/%.c,$(OBJ_DIR)/%.o,$(LINENOISE_SRCS))

TARGET = agent-ej

all: $(TARGET)

$(TARGET): $(OBJS) $(CJSON_OBJS) $(LINENOISE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(VENDOR_DIR)/cJSON/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(VENDOR_DIR)/linenoise/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

nano:
	$(CC) $(CFLAGS) -o agent-ej-nano $(SRCS) $(CJSON_SRCS) $(LINENOISE_SRCS) $(LDFLAGS)

pico:
	$(CC) $(CFLAGS) -DPICO_MODE -o agent-ej-pico \
		src/core/main.c src/core/agent.c src/tools/tool_dispatch.c src/net/http.c src/core/logger.c \
		$(VENDOR_DIR)/cJSON/cJSON.c $(LDFLAGS)

release: OPT = -Oz
release: LDFLAGS = -flto -Wl,--gc-sections -Wl,--strip-all
release: $(TARGET)
	@echo "---"
	ls -lh $(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) agent-ej-nano agent-ej-pico

.PHONY: all clean nano pico
