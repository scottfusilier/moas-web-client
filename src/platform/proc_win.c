/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * On Windows the program's arguments are rebuilt from the UTF-16 command
 * line, and the console is told to expect UTF-8.
 */

#include <windows.h>
#include <shellapi.h>

#include <stdlib.h>

#include "platform/err.h"
#include "platform/proc.h"

int
plat_process_init(int *argc, char ***argv)
{
    int n = 0;
    LPWSTR *wargs = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wargs == NULL || n < 0) {
        err_set_os_code((int)GetLastError());
        return ERR_IO;
    }

    /* One block: the pointer array, then the strings.  Kept until exit. */
    size_t total = ((size_t)n + 1) * sizeof(char *);
    for (int i = 0; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, NULL, 0, NULL,
                                      NULL);
        if (len <= 0) {
            LocalFree(wargs);
            return ERR_INVALID;
        }
        total += (size_t)len;
    }
    char *block = malloc(total);
    if (block == NULL) {
        LocalFree(wargs);
        return ERR_NOMEM;
    }
    char **args = (char **)(void *)block;
    char *text = block + ((size_t)n + 1) * sizeof(char *);
    char *end = block + total;
    for (int i = 0; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, text,
                                      (int)(end - text), NULL, NULL);
        if (len <= 0) {
            free(block);
            LocalFree(wargs);
            return ERR_INVALID;
        }
        args[i] = text;
        text += len;
    }
    args[n] = NULL;
    LocalFree(wargs);

    /* Has no effect when output goes to a file or pipe, which is fine. */
    SetConsoleOutputCP(CP_UTF8);

    *argc = n;
    *argv = args;
    return 0;
}
