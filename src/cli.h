#ifndef BELLWIN_CLI_H
#define BELLWIN_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum BellwinCliAction {
    BELLWIN_CLI_NONE,
    BELLWIN_CLI_RING,
    BELLWIN_CLI_PAUSE,
    BELLWIN_CLI_UNPAUSE,
    BELLWIN_CLI_SHOW,
    BELLWIN_CLI_SET,
    BELLWIN_CLI_GET,
    BELLWIN_CLI_STATUS,
    BELLWIN_CLI_INSTALL,
    BELLWIN_CLI_UNINSTALL,
    BELLWIN_CLI_UNINSTALL_HELPER,
} BellwinCliAction;

typedef struct BellwinCliCommand {
    BellwinCliAction action;
    int pauseMinutes;
    int background;
    uint32_t helperParentHandle;
    uint32_t helperReadyHandle;
    char argument[128];
} BellwinCliCommand;

typedef enum BellwinCliParseResult {
    BELLWIN_CLI_PARSE_ERROR = -1,
    BELLWIN_CLI_PARSE_OK,
    BELLWIN_CLI_PARSE_HELP,
} BellwinCliParseResult;

BellwinCliParseResult bellwin_cli_parse(
    int argc,
    char **argv,
    BellwinCliCommand *command,
    char *error,
    size_t errorCount
);
void bellwin_cli_print_help(FILE *stream);
const char *bellwin_cli_help_text(void);

enum {
    BELLWIN_IPC_PROTOCOL_VERSION = 1,
    BELLWIN_IPC_TEXT_CAPACITY = 2048,
};

typedef struct BellwinIpcRequest {
    uint32_t version;
    uint32_t size;
    BellwinCliAction action;
    int32_t pauseMinutes;
    char argument[128];
} BellwinIpcRequest;

typedef struct BellwinIpcResponse {
    uint32_t version;
    uint32_t size;
    int32_t status;
    char text[BELLWIN_IPC_TEXT_CAPACITY];
} BellwinIpcResponse;

int bellwin_cli_make_request(const BellwinCliCommand *command, BellwinIpcRequest *request);
int bellwin_ipc_request_valid(const BellwinIpcRequest *request, size_t byteCount);
int bellwin_ipc_response_valid(const BellwinIpcResponse *response, size_t byteCount);

typedef enum BellwinSettingKey {
    BELLWIN_SETTING_INVALID,
    BELLWIN_SETTING_VOLUME,
    BELLWIN_SETTING_MINIMUM_INTERVAL,
    BELLWIN_SETTING_MAXIMUM_INTERVAL,
    BELLWIN_SETTING_QUIET_START,
    BELLWIN_SETTING_QUIET_END,
    BELLWIN_SETTING_AUTOSTART,
} BellwinSettingKey;

BellwinSettingKey bellwin_cli_setting_key(const char *text);
const char *bellwin_cli_setting_name(BellwinSettingKey key);
int bellwin_cli_parse_assignment(
    const char *text,
    BellwinSettingKey *key,
    int *value,
    char *error,
    size_t errorCount
);

#endif
