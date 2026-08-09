#include "cli.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IMPLEMENTATION
#include "thirdparty/flag.h"

static void cli_error(char *buffer, size_t count, const char *format, ...) {
    if (!buffer || count == 0) return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, count, format, arguments);
    va_end(arguments);
}

static int cli_copy_argument(
    char *destination,
    size_t destinationCount,
    const char *source,
    char *error,
    size_t errorCount
) {
    if (!source || source[0] == '\0') {
        cli_error(error, errorCount, "the option requires a non-empty value");
        return 0;
    }
    size_t length = strlen(source);
    if (length >= destinationCount) {
        cli_error(error, errorCount, "the option value is too long");
        return 0;
    }
    memcpy(destination, source, length + 1);
    return 1;
}

static int cli_action_is_ipc(BellwinCliAction action) {
    return action >= BELLWIN_CLI_RING && action <= BELLWIN_CLI_STATUS;
}

BellwinCliParseResult bellwin_cli_parse(
    int argc,
    char **argv,
    BellwinCliCommand *command,
    char *error,
    size_t errorCount
) {
    if (!command || argc <= 0 || !argv) {
        cli_error(error, errorCount, "invalid command line");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    memset(command, 0, sizeof(*command));
    if (error && errorCount > 0) error[0] = '\0';

    void *flags = flag_c_new(NULL);
    if (!flags) {
        cli_error(error, errorCount, "out of memory");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    bool *background = flag_c_bool(flags, "background", false, "start without showing Settings");
    bool *ring = flag_c_bool(flags, "ring", false, "ring the bell now");
    uint64_t *pause = flag_c_uint64(flags, "pause", UINT64_MAX, "pause for 30, 60, or 120 minutes");
    bool *unpause = flag_c_bool(flags, "unpause", false, "resume ringing");
    bool *show = flag_c_bool(flags, "show", false, "show Settings");
    char **set = flag_c_str(flags, "set", NULL, "set a setting (key=value)");
    char **get = flag_c_str(flags, "get", NULL, "read a setting by key");
    bool *status = flag_c_bool(flags, "status", false, "print all settings and runtime state");
    bool *install = flag_c_bool(flags, "install", false, "install Bellwin for the current user");
    bool *uninstall = flag_c_bool(flags, "uninstall", false, "uninstall Bellwin for the current user");
    uint64_t *uninstallHelper = flag_c_uint64(
        flags, "uninstall-helper", UINT64_MAX, "internal uninstall cleanup"
    );
    uint64_t *uninstallReady = flag_c_uint64(
        flags, "uninstall-ready", UINT64_MAX, "internal uninstall readiness event"
    );
    bool *help = flag_c_bool(flags, "help", false, "show this help");

    char **normalized = (char **)malloc((size_t)argc * sizeof(*normalized));
    if (!normalized) {
        flag_c_free(flags);
        cli_error(error, errorCount, "out of memory");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    normalized[0] = argv[0];
    for (int i = 1; i < argc; ++i) {
        normalized[i] = argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] != '\0'
            ? argv[i] + 1
            : argv[i];
    }

    int parsed = flag_c_parse(flags, argc, normalized);
    free(normalized);
    if (!parsed) {
        flag_c_free(flags);
        cli_error(error, errorCount, "unknown, invalid, or incomplete option");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    if (flag_c_rest_argc(flags) != 0) {
        flag_c_free(flags);
        cli_error(error, errorCount, "unexpected positional argument");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    if (*help) {
        flag_c_free(flags);
        return BELLWIN_CLI_PARSE_HELP;
    }

    int actionCount = (*ring ? 1 : 0)
        + (*pause != UINT64_MAX ? 1 : 0)
        + (*unpause ? 1 : 0)
        + (*show ? 1 : 0)
        + (*set ? 1 : 0)
        + (*get ? 1 : 0)
        + (*status ? 1 : 0)
        + (*install ? 1 : 0)
        + (*uninstall ? 1 : 0)
        + (*uninstallHelper != UINT64_MAX ? 1 : 0);
    if (actionCount > 1 || (*background && actionCount != 0)) {
        flag_c_free(flags);
        cli_error(error, errorCount, "choose exactly one action");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    if (*pause != UINT64_MAX && *pause != 30 && *pause != 60 && *pause != 120) {
        flag_c_free(flags);
        cli_error(error, errorCount, "--pause must be 30, 60, or 120");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    if (*uninstallHelper != UINT64_MAX
            && (*uninstallHelper == 0 || *uninstallHelper > UINT32_MAX)) {
        flag_c_free(flags);
        cli_error(error, errorCount, "invalid uninstall helper process id");
        return BELLWIN_CLI_PARSE_ERROR;
    }
    if ((*uninstallHelper == UINT64_MAX) != (*uninstallReady == UINT64_MAX)
            || (*uninstallReady != UINT64_MAX
                && (*uninstallReady == 0 || *uninstallReady > UINT32_MAX))) {
        flag_c_free(flags);
        cli_error(error, errorCount, "invalid uninstall helper readiness handle");
        return BELLWIN_CLI_PARSE_ERROR;
    }

    command->background = *background;
    if (*ring) command->action = BELLWIN_CLI_RING;
    else if (*pause != UINT64_MAX) {
        command->action = BELLWIN_CLI_PAUSE;
        command->pauseMinutes = (int)*pause;
    } else if (*unpause) command->action = BELLWIN_CLI_UNPAUSE;
    else if (*show) command->action = BELLWIN_CLI_SHOW;
    else if (*set) command->action = BELLWIN_CLI_SET;
    else if (*get) command->action = BELLWIN_CLI_GET;
    else if (*status) command->action = BELLWIN_CLI_STATUS;
    else if (*install) command->action = BELLWIN_CLI_INSTALL;
    else if (*uninstall) command->action = BELLWIN_CLI_UNINSTALL;
    else if (*uninstallHelper != UINT64_MAX) {
        command->action = BELLWIN_CLI_UNINSTALL_HELPER;
        command->helperParentHandle = (uint32_t)*uninstallHelper;
        command->helperReadyHandle = (uint32_t)*uninstallReady;
    }

    const char *argument = *set ? *set : *get;
    int copied = !argument || cli_copy_argument(
        command->argument, sizeof(command->argument), argument, error, errorCount
    );
    flag_c_free(flags);
    return copied ? BELLWIN_CLI_PARSE_OK : BELLWIN_CLI_PARSE_ERROR;
}

const char *bellwin_cli_help_text(void) {
    return
        "Bellwin command line:\n"
        "  --ring                     Ring the bell now\n"
        "  --pause 30|60|120          Pause ringing\n"
        "  --unpause                  Resume ringing\n"
        "  --show                     Show Settings\n"
        "  --set key=value            Change a setting\n"
        "  --get key                  Read a setting\n"
        "  --status                   Print all settings and runtime state\n"
        "  --install                  Install Bellwin for the current user\n"
        "  --uninstall                Uninstall Bellwin for the current user\n"
        "  --background               Start without showing Settings\n"
        "  --help                     Show this help\n";
}

void bellwin_cli_print_help(FILE *stream) {
    fputs(bellwin_cli_help_text(), stream);
}

int bellwin_cli_make_request(const BellwinCliCommand *command, BellwinIpcRequest *request) {
    if (!command || !request || !cli_action_is_ipc(command->action)) {
        return 0;
    }
    memset(request, 0, sizeof(*request));
    request->version = BELLWIN_IPC_PROTOCOL_VERSION;
    request->size = sizeof(*request);
    request->action = command->action;
    request->pauseMinutes = command->pauseMinutes;
    memcpy(request->argument, command->argument, sizeof(request->argument));
    request->argument[sizeof(request->argument) - 1] = '\0';
    return 1;
}

int bellwin_ipc_request_valid(const BellwinIpcRequest *request, size_t byteCount) {
    if (!request || byteCount != sizeof(*request)
            || request->version != BELLWIN_IPC_PROTOCOL_VERSION
            || request->size != sizeof(*request)
            || !cli_action_is_ipc(request->action)
            || !memchr(request->argument, '\0', sizeof(request->argument))) {
        return 0;
    }
    return request->action != BELLWIN_CLI_PAUSE
        || request->pauseMinutes == 30
        || request->pauseMinutes == 60
        || request->pauseMinutes == 120;
}

int bellwin_ipc_response_valid(const BellwinIpcResponse *response, size_t byteCount) {
    return response
        && byteCount == sizeof(*response)
        && response->version == BELLWIN_IPC_PROTOCOL_VERSION
        && response->size == sizeof(*response)
        && memchr(response->text, '\0', sizeof(response->text));
}

BellwinSettingKey bellwin_cli_setting_key(const char *text) {
    if (!text) return BELLWIN_SETTING_INVALID;
    if (strcmp(text, "volume") == 0) return BELLWIN_SETTING_VOLUME;
    if (strcmp(text, "minimum-interval") == 0) return BELLWIN_SETTING_MINIMUM_INTERVAL;
    if (strcmp(text, "maximum-interval") == 0) return BELLWIN_SETTING_MAXIMUM_INTERVAL;
    if (strcmp(text, "quiet-start") == 0) return BELLWIN_SETTING_QUIET_START;
    if (strcmp(text, "quiet-end") == 0) return BELLWIN_SETTING_QUIET_END;
    if (strcmp(text, "autostart") == 0) return BELLWIN_SETTING_AUTOSTART;
    return BELLWIN_SETTING_INVALID;
}

const char *bellwin_cli_setting_name(BellwinSettingKey key) {
    switch (key) {
    case BELLWIN_SETTING_VOLUME: return "volume";
    case BELLWIN_SETTING_MINIMUM_INTERVAL: return "minimum-interval";
    case BELLWIN_SETTING_MAXIMUM_INTERVAL: return "maximum-interval";
    case BELLWIN_SETTING_QUIET_START: return "quiet-start";
    case BELLWIN_SETTING_QUIET_END: return "quiet-end";
    case BELLWIN_SETTING_AUTOSTART: return "autostart";
    case BELLWIN_SETTING_INVALID: break;
    }
    return NULL;
}

static int cli_parse_integer(const char *text, int *value) {
    if (!text || text[0] == '\0') return 0;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) return 0;
    *value = (int)parsed;
    return 1;
}

static int cli_parse_time(const char *text, int *value) {
    if (!text) return 0;
    const char *colon = strchr(text, ':');
    if (!colon || colon == text || colon - text > 2 || strlen(colon + 1) != 2) return 0;
    int hours = 0;
    for (const char *cursor = text; cursor < colon; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return 0;
        hours = hours * 10 + (*cursor - '0');
    }
    if (colon[1] < '0' || colon[1] > '9' || colon[2] < '0' || colon[2] > '9') return 0;
    int minutes = (colon[1] - '0') * 10 + (colon[2] - '0');
    if (hours > 23 || minutes > 59) return 0;
    *value = hours * 60 + minutes;
    return 1;
}

int bellwin_cli_parse_assignment(
    const char *text,
    BellwinSettingKey *key,
    int *value,
    char *error,
    size_t errorCount
) {
    if (!text || !key || !value) {
        cli_error(error, errorCount, "invalid setting assignment");
        return 0;
    }
    const char *equals = strchr(text, '=');
    size_t keyLength = equals ? (size_t)(equals - text) : 0;
    if (!equals || keyLength == 0 || keyLength >= 32 || equals[1] == '\0') {
        cli_error(error, errorCount, "--set expects key=value");
        return 0;
    }
    char keyText[32];
    memcpy(keyText, text, keyLength);
    keyText[keyLength] = '\0';
    *key = bellwin_cli_setting_key(keyText);
    if (*key == BELLWIN_SETTING_INVALID) {
        cli_error(error, errorCount, "unknown setting: %s", keyText);
        return 0;
    }

    const char *valueText = equals + 1;
    int parsed = 0;
    if (*key == BELLWIN_SETTING_QUIET_START || *key == BELLWIN_SETTING_QUIET_END) {
        parsed = cli_parse_time(valueText, value);
    } else if (*key == BELLWIN_SETTING_AUTOSTART) {
        if (strcmp(valueText, "on") == 0 || strcmp(valueText, "true") == 0
                || strcmp(valueText, "1") == 0) {
            *value = 1;
            parsed = 1;
        } else if (strcmp(valueText, "off") == 0 || strcmp(valueText, "false") == 0
                || strcmp(valueText, "0") == 0) {
            *value = 0;
            parsed = 1;
        }
    } else {
        parsed = cli_parse_integer(valueText, value);
    }
    if (!parsed) {
        cli_error(error, errorCount, "invalid value for %s", keyText);
        return 0;
    }

    int inRange = 1;
    if (*key == BELLWIN_SETTING_VOLUME) inRange = *value >= 0 && *value <= 100;
    else if (*key == BELLWIN_SETTING_MINIMUM_INTERVAL
            || *key == BELLWIN_SETTING_MAXIMUM_INTERVAL) {
        inRange = *value >= 30 && *value <= 480 && *value % 30 == 0;
    }
    if (!inRange) {
        cli_error(error, errorCount, "value out of range for %s", keyText);
        return 0;
    }
    return 1;
}
