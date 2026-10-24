/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The platform layer's files and clock.  Runs in its build folder (CTest
 * sets the working directory), where it creates and removes its own files.
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/clock.h"
#include "platform/fs.h"
#include "test.h"

/* A name that is not plain ASCII, to check UTF-8 paths on Windows. */
#define FILE_NAME "test_fs_r\xc3\xa9glages.json"
#define FOLDER_NAME "test_fs_folder"

static void
test_replace_and_read(void)
{
    plat_remove_file(FILE_NAME);
    char *data = NULL;
    size_t len = 0;

    CHECK_INT_EQ(plat_read_file(FILE_NAME, 1000, &data, &len), ERR_NOT_FOUND);

    const char first[] = "{\"Server\":\"192.168.1.10:12059\"}";
    CHECK_INT_EQ(plat_replace_file(FILE_NAME, first, sizeof(first) - 1), 0);
    CHECK_INT_EQ(plat_read_file(FILE_NAME, 1000, &data, &len), 0);
    if (data != NULL) {
        CHECK_MEM_EQ(data, len, "{\"Server\":\"192.168.1.10:12059\"}");
        CHECK(data[len] == '\0');
        free(data);
        data = NULL;
    }

    /* Replacing an existing file. */
    const char second[] = "{}";
    CHECK_INT_EQ(plat_replace_file(FILE_NAME, second, 2), 0);
    CHECK_INT_EQ(plat_read_file(FILE_NAME, 1000, &data, &len), 0);
    if (data != NULL) {
        CHECK_MEM_EQ(data, len, "{}");
        free(data);
        data = NULL;
    }

    /* No temporary file is left behind. */
    CHECK_INT_EQ(plat_read_file(FILE_NAME ".tmp", 1000, &data, &len),
                 ERR_NOT_FOUND);

    /* An empty file. */
    CHECK_INT_EQ(plat_replace_file(FILE_NAME, "", 0), 0);
    CHECK_INT_EQ(plat_read_file(FILE_NAME, 1000, &data, &len), 0);
    CHECK_INT_EQ(len, 0);
    free(data);
    data = NULL;

    CHECK_INT_EQ(plat_remove_file(FILE_NAME), 0);
    CHECK_INT_EQ(plat_remove_file(FILE_NAME), ERR_NOT_FOUND);
    CHECK_INT_EQ(plat_read_file(FILE_NAME, 1000, &data, &len), ERR_NOT_FOUND);
}

static void
test_read_limit(void)
{
    char *data = NULL;
    size_t len = 0;
    const char text[] = "0123456789";

    CHECK_INT_EQ(plat_replace_file(FILE_NAME, text, 10), 0);
    /* Exactly the limit is fine; one byte less is too small. */
    CHECK_INT_EQ(plat_read_file(FILE_NAME, 10, &data, &len), 0);
    free(data);
    data = NULL;
    CHECK_INT_EQ(plat_read_file(FILE_NAME, 9, &data, &len), ERR_LIMIT);
    CHECK(data == NULL);
    /* A limit so large that the buffer size would wrap around. */
    CHECK_INT_EQ(plat_read_file(FILE_NAME, SIZE_MAX, &data, &len),
                 ERR_INVALID);
    plat_remove_file(FILE_NAME);
}

static void
test_folders(void)
{
    char dir[4096];

    CHECK_INT_EQ(plat_settings_dir(dir, sizeof(dir)), 0);
    size_t n = strlen(dir);
    CHECK(n > 1);
    CHECK(dir[n - 1] != '/' && dir[n - 1] != '\\');
    CHECK_INT_EQ(plat_settings_dir(dir, 3), ERR_LIMIT);

    CHECK_INT_EQ(plat_make_dir(FOLDER_NAME), 0);
    CHECK_INT_EQ(plat_make_dir(FOLDER_NAME), 0); /* already there: fine */
    CHECK(plat_path_separator() == '/' || plat_path_separator() == '\\');
    /* A folder is not a file. */
    char *data = NULL;
    size_t len = 0;
    CHECK(plat_read_file(FOLDER_NAME, 1000, &data, &len) != 0);
    CHECK(data == NULL);
}

static void
test_clock(void)
{
    uint64_t start = plat_monotonic_ms();
    plat_sleep_ms(50);
    uint64_t elapsed = plat_monotonic_ms() - start;
    /* Windows' clock ticks in steps of about 16 ms. */
    CHECK(elapsed >= 30);
    CHECK(elapsed < 5000);

    struct plat_local_time t;
    plat_local_time(&t);
    CHECK(t.year >= 2024);
    CHECK(t.month >= 1 && t.month <= 12);
    CHECK(t.day >= 1 && t.day <= 31);
    CHECK(t.millisecond >= 0 && t.millisecond <= 999);
}

int
main(void)
{
    RUN(test_replace_and_read);
    RUN(test_read_limit);
    RUN(test_folders);
    RUN(test_clock);
    return test_summary();
}
