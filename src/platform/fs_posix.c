/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Files and folders on FreeBSD, Linux and macOS. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "platform/err.h"
#include "platform/fs.h"

static int
from_errno(int e)
{
    err_set_os_code(e);
    if (e == ENOENT || e == ENOTDIR) {
        return ERR_NOT_FOUND;
    }
    if (e == EACCES || e == EPERM || e == EROFS) {
        return ERR_ACCESS;
    }
    if (e == ENOMEM) {
        return ERR_NOMEM;
    }
    if (e == ENAMETOOLONG) {
        return ERR_LIMIT;
    }
    return ERR_IO;
}

int
plat_read_file(const char *path, size_t max, char **data, size_t *len)
{
    int fd;

    /* The buffer is max + 2 bytes; keep that from wrapping around. */
    if (max > SIZE_MAX - 2) {
        return ERR_INVALID;
    }
    do {
        fd = open(path, O_RDONLY | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) {
        return from_errno(errno);
    }

    int err = 0;
    char *buf = NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        err = from_errno(errno);
        goto out;
    }
    if (!S_ISREG(st.st_mode)) {
        err = ERR_INVALID;
        goto out;
    }
    if (st.st_size < 0 || (unsigned long long)st.st_size > max) {
        err = ERR_LIMIT;
        goto out;
    }

    /*
     * Read up to max + 1 bytes: the file may have grown since fstat(), and
     * one byte more than max shows that it is too large.
     */
    buf = malloc(max + 2);
    if (buf == NULL) {
        err = ERR_NOMEM;
        goto out;
    }
    size_t total = 0;
    while (total <= max) {
        ssize_t n = read(fd, buf + total, max + 1 - total);
        if (n == -1 && errno == EINTR) {
            continue;
        }
        if (n == -1) {
            err = from_errno(errno);
            goto out;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
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
    close(fd);
    return err;
}

static int
write_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n == -1 && errno == EINTR) {
            continue;
        }
        if (n == -1) {
            return from_errno(errno);
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Flushes the folder holding path, so that a rename survives a crash. */
static void
sync_parent(const char *path)
{
    const char *slash = strrchr(path, '/');
    char dir[4096];

    if (slash == NULL) {
        snprintf(dir, sizeof(dir), ".");
    } else if (slash == path) {
        snprintf(dir, sizeof(dir), "/");
    } else {
        size_t n = (size_t)(slash - path);
        if (n >= sizeof(dir)) {
            return;
        }
        memcpy(dir, path, n);
        dir[n] = '\0';
    }
    int fd = open(dir, O_RDONLY | O_CLOEXEC);
    if (fd != -1) {
        /* Best effort: some file systems do not support this. */
        fsync(fd);
        close(fd);
    }
}

int
plat_replace_file(const char *path, const void *data, size_t len)
{
    char tmp[4096];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        return ERR_LIMIT;
    }

    mode_t mode = 0644;
    struct stat st;
    if (stat(path, &st) == 0) {
        mode = st.st_mode & 07777;
    }

    int fd;
    do {
        fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) {
        return from_errno(errno);
    }
    int err = write_all(fd, data, len);
    if (err == 0 && fsync(fd) != 0) {
        err = from_errno(errno);
    }
    if (close(fd) != 0 && err == 0) {
        err = from_errno(errno);
    }
    if (err == 0 && rename(tmp, path) != 0) {
        err = from_errno(errno);
    }
    if (err != 0) {
        unlink(tmp);
        return err;
    }
    sync_parent(path);
    return 0;
}

int
plat_settings_dir(char *out, size_t size)
{
    int n;

#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (home == NULL || home[0] != '/') {
        return ERR_NOT_FOUND;
    }
    n = snprintf(out, size, "%s/Library/Application Support", home);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] == '/') {
        n = snprintf(out, size, "%s", xdg);
    } else {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] != '/') {
            return ERR_NOT_FOUND;
        }
        n = snprintf(out, size, "%s/.config", home);
    }
#endif
    if (n < 0 || (size_t)n >= size) {
        return ERR_LIMIT;
    }
    /* No trailing separator, except for the root folder itself. */
    size_t len = (size_t)n;
    while (len > 1 && out[len - 1] == '/') {
        out[--len] = '\0';
    }
    return 0;
}

int
plat_remove_file(const char *path)
{
    if (unlink(path) != 0) {
        return from_errno(errno);
    }
    return 0;
}

int
plat_make_dir(const char *path)
{
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    int e = errno;
    struct stat st;
    if (e == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;
    }
    return from_errno(e);
}

char
plat_path_separator(void)
{
    return '/';
}
