/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * On Unix, arguments and console text are already in the user's locale,
 * which is UTF-8 on any current system; nothing needs converting.
 */

#include "platform/proc.h"

int
plat_process_init(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    return 0;
}
