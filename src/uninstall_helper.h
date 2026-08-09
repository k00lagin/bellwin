#ifndef BELLWIN_UNINSTALL_HELPER_H
#define BELLWIN_UNINSTALL_HELPER_H

#include <stddef.h>
#include <wchar.h>

int bellwin_create_temporary_executable_copy(
    const wchar_t *sourcePath,
    wchar_t *temporaryPath,
    size_t temporaryPathCount
);

int bellwin_start_temporary_file_cleanup(const wchar_t *targetPath);

#endif
