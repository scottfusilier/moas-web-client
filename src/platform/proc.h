/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Setting up the process: command-line arguments and console output. */

#ifndef MOAS_PLATFORM_PROC_H
#define MOAS_PLATFORM_PROC_H

/*
 * Call first thing in main().  Afterwards *argv holds the command-line
 * arguments as UTF-8 on every system, and text written to the console as
 * UTF-8 shows correctly.  (On Windows main() receives the arguments in the
 * local code page, and the console expects it too.)  The new arguments
 * last until the program ends.
 */
int plat_process_init(int *argc, char ***argv);

#endif /* MOAS_PLATFORM_PROC_H */
