/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Checks that the vendored libraries build and work on this system, using
 * input shaped like what moas-web-client will give them.
 */

#include <string.h>

#include "cJSON.h"
#include "picohttpparser.h"
#include "test.h"

static void
test_cjson_reads_oasp_message(void)
{
    const char text[] = "{ \"Message\" : \"TR\", \"Sequence\" : 5, "
                        "\"Version\" : 1, \"Parameters\" : "
                        "{ \"Station\" : 1, \"Radio\" : 2, \"TR\" : 3 } }";

    cJSON *msg = cJSON_ParseWithLength(text, sizeof(text) - 1);
    CHECK(msg != NULL);
    if (msg == NULL) {
        return;
    }
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(msg, "Message");
    CHECK(cJSON_IsString(name));
    if (cJSON_IsString(name)) {
        CHECK_MEM_EQ(name->valuestring, strlen(name->valuestring), "TR");
    }
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "Parameters");
    const cJSON *tr = cJSON_GetObjectItemCaseSensitive(params, "TR");
    CHECK(cJSON_IsNumber(tr));
    if (cJSON_IsNumber(tr)) {
        CHECK_INT_EQ(tr->valueint, 3);
    }
    cJSON_Delete(msg);
}

static void
test_cjson_rejects_truncated_input(void)
{
    const char text[] = "{ \"Message\" : ";

    CHECK(cJSON_ParseWithLength(text, sizeof(text) - 1) == NULL);
}

static void
test_cjson_writes_unformatted(void)
{
    cJSON *obj = cJSON_CreateObject();
    CHECK(obj != NULL);
    if (obj == NULL) {
        return;
    }
    CHECK(cJSON_AddStringToObject(obj, "Message", "Radio") != NULL);
    CHECK(cJSON_AddNumberToObject(obj, "Sequence", 2) != NULL);
    char *text = cJSON_PrintUnformatted(obj);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK_MEM_EQ(text, strlen(text),
                     "{\"Message\":\"Radio\",\"Sequence\":2}");
        cJSON_free(text);
    }
    cJSON_Delete(obj);
}

static void
test_picohttpparser_reads_request(void)
{
    const char req[] = "POST /api/select HTTP/1.1\r\n"
                       "Host: 127.0.0.1:8080\r\n"
                       "Content-Length: 2\r\n"
                       "\r\n"
                       "{}";
    const char *method = NULL;
    size_t method_len = 0;
    const char *path = NULL;
    size_t path_len = 0;
    int minor_version = -1;
    struct phr_header headers[8];
    size_t num_headers = sizeof(headers) / sizeof(headers[0]);

    int n = phr_parse_request(req, sizeof(req) - 1, &method, &method_len,
                              &path, &path_len, &minor_version, headers,
                              &num_headers, 0);
    /* The header block ends just before the two-byte body. */
    CHECK_INT_EQ(n, (int)(sizeof(req) - 1 - 2));
    if (n <= 0) {
        return;
    }
    CHECK_MEM_EQ(method, method_len, "POST");
    CHECK_MEM_EQ(path, path_len, "/api/select");
    CHECK_INT_EQ(minor_version, 1);
    CHECK_INT_EQ(num_headers, 2);
    CHECK_MEM_EQ(headers[0].name, headers[0].name_len, "Host");
    CHECK_MEM_EQ(headers[0].value, headers[0].value_len, "127.0.0.1:8080");
}

static void
test_picohttpparser_waits_for_more(void)
{
    const char req[] = "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1";
    const char *method = NULL;
    size_t method_len = 0;
    const char *path = NULL;
    size_t path_len = 0;
    int minor_version = -1;
    struct phr_header headers[8];
    size_t num_headers = sizeof(headers) / sizeof(headers[0]);

    /* -2 means the request is incomplete so far. */
    int n = phr_parse_request(req, sizeof(req) - 1, &method, &method_len,
                              &path, &path_len, &minor_version, headers,
                              &num_headers, 0);
    CHECK_INT_EQ(n, -2);
}

int
main(void)
{
    RUN(test_cjson_reads_oasp_message);
    RUN(test_cjson_rejects_truncated_input);
    RUN(test_cjson_writes_unformatted);
    RUN(test_picohttpparser_reads_request);
    RUN(test_picohttpparser_waits_for_more);
    return test_summary();
}
