/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Files and folders.  Paths are UTF-8 on every system.  Use these rather
 * than the C library's fopen(), remove() and the like, which on Windows
 * take paths in the local code page, not UTF-8.  Functions return 0
 * or an error code from platform/err.h; ERR_NOT_FOUND means the file or
 * folder does not exist, ERR_ACCESS that permission was refused.
 */

#ifndef MOAS_PLATFORM_FS_H
#define MOAS_PLATFORM_FS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Reads a whole regular file.  *data receives memory from malloc() holding
 * the contents followed by a NUL byte, which the caller frees with free();
 * *len excludes the NUL.  A file larger than max bytes is not read and
 * gives ERR_LIMIT.
 */
int plat_read_file(const char *path, size_t max, char **data, size_t *len);

/*
 * Replaces the file at path with data in one step: the new contents are
 * written to a temporary file next to it, flushed to disk, and moved over
 * the old file, so that a reader sees either the old file or the new one,
 * never a partial one.  On Unix a new file is readable and writable by its
 * owner and readable by others (subject to the umask); an existing file
 * keeps its permission bits.
 *
 * On Unix, replacing a file needs write permission on its folder, not on
 * the file itself; callers that must respect a read-only file check that
 * first.
 */
int plat_replace_file(const char *path, const void *data, size_t len);

/*
 * The folder where per-user settings belong:
 *
 *   Windows        %APPDATA% (for example C:\Users\Name\AppData\Roaming)
 *   macOS          ~/Library/Application Support
 *   FreeBSD, Linux $XDG_CONFIG_HOME if it is an absolute path, otherwise
 *                  ~/.config
 *
 * Writes it, NUL-terminated and with no trailing separator, into out.
 * ERR_LIMIT if it does not fit; ERR_NOT_FOUND if it cannot be determined
 * (no home folder).
 */
int plat_settings_dir(char *out, size_t size);

/*
 * Whether this user may write the existing file at path: *writable is
 * false for a read-only file.  ERR_NOT_FOUND if there is no such file.
 */
int plat_file_writable(const char *path, bool *writable);

/*
 * The folder holding the running program, found from argv0 (main()'s
 * argv[0], after plat_process_init()).  ERR_NOT_FOUND if it cannot be
 * told, as when the program was found on the PATH on Unix.
 */
int plat_exe_dir(const char *argv0, char *out, size_t size);

/* Makes a file read-only, or writable again. */
int plat_set_read_only(const char *path, bool read_only);

/* Deletes a file.  ERR_NOT_FOUND if there is none. */
int plat_remove_file(const char *path);

/* Creates a folder; succeeds if it already exists. */
int plat_make_dir(const char *path);

/* The path separator: '\\' on Windows, '/' elsewhere. */
char plat_path_separator(void);

#endif /* MOAS_PLATFORM_FS_H */
