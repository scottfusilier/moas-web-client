/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Files and folders on Windows.  Paths arrive as UTF-8 and are converted to
 * UTF-16 for the wide-character ("W") functions, so any file name works.
 */

#include <windows.h>
#include <shlobj.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/err.h"
#include "platform/fs.h"

/* Longest path accepted, in UTF-16 units, including the NUL. */
#define WIDE_PATH_MAX 32768

static int
from_win(DWORD e)
{
    err_set_os_code((int)e);
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return ERR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_WRITE_PROTECT:
        return ERR_ACCESS;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ERR_NOMEM;
    case ERROR_FILENAME_EXCED_RANGE:
        return ERR_LIMIT;
    default:
        return ERR_IO;
    }
}

/*
 * UTF-8 to a malloc()ed UTF-16 string.  suffix (ASCII, may be NULL) is
 * appended.  Invalid UTF-8 gives ERR_INVALID.
 */
static int
widen(const char *utf8, const char *suffix, wchar_t **out)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL,
                                0);
    if (n <= 0) {
        return ERR_INVALID;
    }
    size_t extra = suffix != NULL ? strlen(suffix) : 0;
    if ((size_t)n + extra > WIDE_PATH_MAX) {
        return ERR_LIMIT;
    }
    wchar_t *w = malloc(((size_t)n + extra) * sizeof(wchar_t));
    if (w == NULL) {
        return ERR_NOMEM;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, w, n) !=
        n)
    {
        free(w);
        return ERR_INVALID;
    }
    /* n counts the NUL; write the suffix over it and end again. */
    for (size_t i = 0; i < extra; i++) {
        w[(size_t)n - 1 + i] = (wchar_t)(unsigned char)suffix[i];
    }
    w[(size_t)n - 1 + extra] = L'\0';
    *out = w;
    return 0;
}

int
plat_read_file(const char *path, size_t max, char **data, size_t *len)
{
    /* The buffer is max + 2 bytes; keep that from wrapping around. */
    if (max > SIZE_MAX - 2) {
        return ERR_INVALID;
    }
    wchar_t *wpath;
    int err = widen(path, NULL, &wpath);
    if (err != 0) {
        return err;
    }
    HANDLE h = CreateFileW(wpath, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE |
                               FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) {
        return from_win(GetLastError());
    }

    char *buf = NULL;
    LARGE_INTEGER size;
    if (GetFileType(h) != FILE_TYPE_DISK) {
        err = ERR_INVALID;
        goto out;
    }
    if (!GetFileSizeEx(h, &size)) {
        err = from_win(GetLastError());
        goto out;
    }
    if (size.QuadPart < 0 || (unsigned long long)size.QuadPart > max) {
        err = ERR_LIMIT;
        goto out;
    }

    /* As on Unix: read one byte more than max to catch a growing file. */
    buf = malloc(max + 2);
    if (buf == NULL) {
        err = ERR_NOMEM;
        goto out;
    }
    size_t total = 0;
    while (total <= max) {
        size_t want = max + 1 - total;
        DWORD chunk = want > 0x40000000u ? 0x40000000u : (DWORD)want;
        DWORD got = 0;
        if (!ReadFile(h, buf + total, chunk, &got, NULL)) {
            err = from_win(GetLastError());
            goto out;
        }
        if (got == 0) {
            break;
        }
        total += got;
    }
    if (total > max) {
        err = ERR_LIMIT;
        goto out;
    }
    buf[total] = '\0';
    *data = buf;
    *len = total;
    buf = NULL;

out:
    free(buf);
    CloseHandle(h);
    return err;
}

int
plat_replace_file(const char *path, const void *data, size_t len)
{
    wchar_t *wpath = NULL;
    wchar_t *wtmp = NULL;
    int err = widen(path, NULL, &wpath);
    if (err == 0) {
        err = widen(path, ".tmp", &wtmp);
    }
    if (err != 0) {
        free(wpath);
        return err;
    }

    HANDLE h = CreateFileW(wtmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        err = from_win(GetLastError());
        goto out;
    }
    const char *p = data;
    while (len > 0 && err == 0) {
        DWORD chunk = len > 0x40000000u ? 0x40000000u : (DWORD)len;
        DWORD wrote = 0;
        if (!WriteFile(h, p, chunk, &wrote, NULL)) {
            err = from_win(GetLastError());
            break;
        }
        p += wrote;
        len -= wrote;
    }
    if (err == 0 && !FlushFileBuffers(h)) {
        err = from_win(GetLastError());
    }
    CloseHandle(h);
    if (err == 0 &&
        !MoveFileExW(wtmp, wpath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        err = from_win(GetLastError());
    }
    if (err != 0) {
        DeleteFileW(wtmp);
    }

out:
    free(wtmp);
    free(wpath);
    return err;
}

int
plat_settings_dir(char *out, size_t size)
{
    PWSTR wdir = NULL;

    if (SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &wdir) != S_OK)
    {
        CoTaskMemFree(wdir);
        return ERR_NOT_FOUND;
    }
    int err = 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, wdir, -1, NULL, 0, NULL, NULL);
    if (n <= 0) {
        err = ERR_INVALID;
    } else if ((size_t)n > size) {
        err = ERR_LIMIT;
    } else if (WideCharToMultiByte(CP_UTF8, 0, wdir, -1, out, n, NULL, NULL) !=
               n)
    {
        err = ERR_INVALID;
    }
    CoTaskMemFree(wdir);
    return err;
}

int
plat_remove_file(const char *path)
{
    wchar_t *wpath;
    int err = widen(path, NULL, &wpath);
    if (err != 0) {
        return err;
    }
    if (!DeleteFileW(wpath)) {
        err = from_win(GetLastError());
    }
    free(wpath);
    return err;
}

int
plat_make_dir(const char *path)
{
    wchar_t *wpath;
    int err = widen(path, NULL, &wpath);
    if (err != 0) {
        return err;
    }
    if (!CreateDirectoryW(wpath, NULL)) {
        DWORD e = GetLastError();
        DWORD attrs = GetFileAttributesW(wpath);
        if (e != ERROR_ALREADY_EXISTS || attrs == INVALID_FILE_ATTRIBUTES ||
            (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            err = from_win(e);
        }
    }
    free(wpath);
    return err;
}

char
plat_path_separator(void)
{
    return '\\';
}
