/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The web page, built into the program from page.html by cmake/embed.cmake.
 * page.html is the Go version's page, unchanged.
 */

#ifndef MOAS_APP_PAGE_H
#define MOAS_APP_PAGE_H

#include <stddef.h>

/* The page's bytes, followed by a NUL that app_page_html_len leaves out. */
extern const unsigned char app_page_html[];
extern const size_t app_page_html_len;

#endif /* MOAS_APP_PAGE_H */
