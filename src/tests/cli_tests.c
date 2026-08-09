#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../cli.h"

static BellwinCliParseResult parse(
    int argc,
    char **argv,
    BellwinCliCommand *command
) {
    char error[128];
    return bellwin_cli_parse(argc, argv, command, error, sizeof(error));
}

static void test_no_arguments_starts_the_app(void) {
    char program[] = "Bellwin.exe";
    char *argv[] = {program};
    BellwinCliCommand command;
    assert(parse(1, argv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_NONE);
    assert(!command.background);
}

static void test_action_flags(void) {
    char program[] = "Bellwin.exe";
    char ring[] = "--ring";
    char *ringArgv[] = {program, ring};
    BellwinCliCommand command;
    assert(parse(2, ringArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_RING);

    char unpause[] = "--unpause";
    char *unpauseArgv[] = {program, unpause};
    assert(parse(2, unpauseArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_UNPAUSE);

    char show[] = "--show";
    char *showArgv[] = {program, show};
    assert(parse(2, showArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_SHOW);

    char status[] = "--status";
    char *statusArgv[] = {program, status};
    assert(parse(2, statusArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_STATUS);

    char install[] = "-install";
    char *installArgv[] = {program, install};
    assert(parse(2, installArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_INSTALL);

    char uninstall[] = "--uninstall";
    char *uninstallArgv[] = {program, uninstall};
    assert(parse(2, uninstallArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_UNINSTALL);
}

static void test_pause_values(void) {
    char program[] = "Bellwin.exe";
    char pause[] = "--pause";
    char sixty[] = "60";
    char *argv[] = {program, pause, sixty};
    BellwinCliCommand command;
    assert(parse(3, argv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_PAUSE);
    assert(command.pauseMinutes == 60);

    char invalid[] = "45";
    argv[2] = invalid;
    assert(parse(3, argv, &command) == BELLWIN_CLI_PARSE_ERROR);

    char zero[] = "0";
    argv[2] = zero;
    assert(parse(3, argv, &command) == BELLWIN_CLI_PARSE_ERROR);
}

static void test_set_and_get_arguments(void) {
    char program[] = "Bellwin.exe";
    char set[] = "--set=volume=40";
    char *setArgv[] = {program, set};
    BellwinCliCommand command;
    assert(parse(2, setArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_SET);
    assert(strcmp(command.argument, "volume=40") == 0);

    char get[] = "--get";
    char key[] = "quiet-start";
    char *getArgv[] = {program, get, key};
    assert(parse(3, getArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_GET);
    assert(strcmp(command.argument, "quiet-start") == 0);
}

static void test_background_and_help(void) {
    char program[] = "Bellwin.exe";
    char background[] = "--background";
    char *backgroundArgv[] = {program, background};
    BellwinCliCommand command;
    assert(parse(2, backgroundArgv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_NONE);
    assert(command.background);

    char help[] = "--help";
    char *helpArgv[] = {program, help};
    assert(parse(2, helpArgv, &command) == BELLWIN_CLI_PARSE_HELP);
    assert(strstr(bellwin_cli_help_text(), "--install") != NULL);
    assert(strstr(bellwin_cli_help_text(), "--uninstall") != NULL);
    assert(strstr(bellwin_cli_help_text(), "uninstall-helper") == NULL);
    assert(strstr(bellwin_cli_help_text(), "uninstall-ready") == NULL);
}

static void test_internal_uninstall_helper_validation(void) {
    char program[] = "Bellwin.exe";
    char helper[] = "--uninstall-helper";
    char processId[] = "42";
    char ready[] = "--uninstall-ready";
    char readyHandle[] = "43";
    char *argv[] = {program, helper, processId, ready, readyHandle};
    BellwinCliCommand command;
    assert(parse(5, argv, &command) == BELLWIN_CLI_PARSE_OK);
    assert(command.action == BELLWIN_CLI_UNINSTALL_HELPER);
    assert(command.helperParentHandle == 42);
    assert(command.helperReadyHandle == 43);

    char zero[] = "0";
    argv[2] = zero;
    assert(parse(5, argv, &command) == BELLWIN_CLI_PARSE_ERROR);

    char tooLarge[] = "4294967296";
    argv[2] = tooLarge;
    assert(parse(5, argv, &command) == BELLWIN_CLI_PARSE_ERROR);

    assert(parse(3, argv, &command) == BELLWIN_CLI_PARSE_ERROR);
}

static void test_rejects_ambiguous_or_unknown_commands(void) {
    char program[] = "Bellwin.exe";
    char ring[] = "--ring";
    char show[] = "--show";
    char *ambiguous[] = {program, ring, show};
    BellwinCliCommand command;
    assert(parse(3, ambiguous, &command) == BELLWIN_CLI_PARSE_ERROR);

    char install[] = "-install";
    char uninstall[] = "-uninstall";
    char *conflictingInstallActions[] = {program, install, uninstall};
    assert(parse(3, conflictingInstallActions, &command) == BELLWIN_CLI_PARSE_ERROR);

    char unknown[] = "--wat";
    char *unknownArgv[] = {program, unknown};
    assert(parse(2, unknownArgv, &command) == BELLWIN_CLI_PARSE_ERROR);

    char positional[] = "extra";
    char *positionalArgv[] = {program, positional};
    assert(parse(2, positionalArgv, &command) == BELLWIN_CLI_PARSE_ERROR);
}

static void test_commands_become_versioned_ipc_requests(void) {
    BellwinCliCommand command = {
        .action = BELLWIN_CLI_PAUSE,
        .pauseMinutes = 120,
    };
    BellwinIpcRequest request;
    assert(bellwin_cli_make_request(&command, &request));
    assert(request.version == BELLWIN_IPC_PROTOCOL_VERSION);
    assert(request.size == sizeof(request));
    assert(request.action == BELLWIN_CLI_PAUSE);
    assert(request.pauseMinutes == 120);
    assert(bellwin_ipc_request_valid(&request, sizeof(request)));
    assert(!bellwin_ipc_request_valid(&request, sizeof(request) - 1));

    command.action = BELLWIN_CLI_SET;
    memcpy(command.argument, "volume=40", sizeof("volume=40"));
    assert(bellwin_cli_make_request(&command, &request));
    assert(strcmp(request.argument, "volume=40") == 0);

    command.action = BELLWIN_CLI_NONE;
    assert(!bellwin_cli_make_request(&command, &request));

    command.action = BELLWIN_CLI_INSTALL;
    assert(!bellwin_cli_make_request(&command, &request));

    command.action = BELLWIN_CLI_UNINSTALL;
    assert(!bellwin_cli_make_request(&command, &request));
}

static void test_ipc_responses_are_size_and_version_checked(void) {
    BellwinIpcResponse response = {
        .version = BELLWIN_IPC_PROTOCOL_VERSION,
        .size = sizeof(response),
        .status = 0,
    };
    memcpy(response.text, "volume=40\n", sizeof("volume=40\n"));
    assert(bellwin_ipc_response_valid(&response, sizeof(response)));
    response.version += 1;
    assert(!bellwin_ipc_response_valid(&response, sizeof(response)));
}

static void test_setting_keys_and_assignments(void) {
    assert(bellwin_cli_setting_key("volume") == BELLWIN_SETTING_VOLUME);
    assert(bellwin_cli_setting_key("minimum-interval") == BELLWIN_SETTING_MINIMUM_INTERVAL);
    assert(bellwin_cli_setting_key("maximum-interval") == BELLWIN_SETTING_MAXIMUM_INTERVAL);
    assert(bellwin_cli_setting_key("quiet-start") == BELLWIN_SETTING_QUIET_START);
    assert(bellwin_cli_setting_key("quiet-end") == BELLWIN_SETTING_QUIET_END);
    assert(bellwin_cli_setting_key("autostart") == BELLWIN_SETTING_AUTOSTART);
    assert(bellwin_cli_setting_key("missing") == BELLWIN_SETTING_INVALID);

    BellwinSettingKey key;
    int value;
    char error[128];
    assert(bellwin_cli_parse_assignment("volume=40", &key, &value, error, sizeof(error)));
    assert(key == BELLWIN_SETTING_VOLUME && value == 40);
    assert(bellwin_cli_parse_assignment("minimum-interval=120", &key, &value, error, sizeof(error)));
    assert(key == BELLWIN_SETTING_MINIMUM_INTERVAL && value == 120);
    assert(bellwin_cli_parse_assignment("quiet-start=21:05", &key, &value, error, sizeof(error)));
    assert(key == BELLWIN_SETTING_QUIET_START && value == 21 * 60 + 5);
    assert(bellwin_cli_parse_assignment("autostart=on", &key, &value, error, sizeof(error)));
    assert(key == BELLWIN_SETTING_AUTOSTART && value == 1);
    assert(bellwin_cli_parse_assignment("autostart=off", &key, &value, error, sizeof(error)));
    assert(key == BELLWIN_SETTING_AUTOSTART && value == 0);

    assert(!bellwin_cli_parse_assignment("volume=101", &key, &value, error, sizeof(error)));
    assert(!bellwin_cli_parse_assignment("minimum-interval=45", &key, &value, error, sizeof(error)));
    assert(!bellwin_cli_parse_assignment("quiet-start=24:00", &key, &value, error, sizeof(error)));
    assert(!bellwin_cli_parse_assignment("missing=1", &key, &value, error, sizeof(error)));
    assert(!bellwin_cli_parse_assignment("volume", &key, &value, error, sizeof(error)));
}

int main(void) {
    test_no_arguments_starts_the_app();
    test_action_flags();
    test_pause_values();
    test_set_and_get_arguments();
    test_background_and_help();
    test_internal_uninstall_helper_validation();
    test_rejects_ambiguous_or_unknown_commands();
    test_commands_become_versioned_ipc_requests();
    test_ipc_responses_are_size_and_version_checked();
    test_setting_keys_and_assignments();
    printf("cli tests passed\n");
    return 0;
}
