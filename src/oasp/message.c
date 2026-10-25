/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Decoding the server's messages and encoding the client's. */

#include "core/common.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "oasp/oasp.h"

const char *
oasp_availability_name(int availability)
{
    switch (availability) {
    case OASP_UNAVAILABLE:
        return "unavailable";
    case OASP_AVAILABLE:
        return "available";
    case OASP_SELECTED:
        return "selected";
    default:
        return "unknown";
    }
}

const char *
oasp_tr_name(int tr)
{
    switch (tr) {
    case OASP_TR_UNAVAILABLE:
        return "unavailable";
    case OASP_TR_INHIBITED:
        return "inhibited";
    case OASP_TR_RECEIVE:
        return "receive";
    case OASP_TR_TRANSMIT:
        return "transmit";
    default:
        return "";
    }
}

/*
 * Decoding.  Each decode_ function reads one kind of parameters into out,
 * which starts zeroed, and returns 0 or the first error.  On an error the
 * caller frees whatever was allocated.
 */

/* Shorthand: an int field with no range limit beyond what int holds. */
static int
get_int(const cJSON *obj, const char *key, int *out)
{
    return json_get_int(obj, key, INT_MIN, INT_MAX, out);
}

#define TRY(expr)                                                             \
    do {                                                                      \
        int try_err_ = (expr);                                                \
        if (try_err_ != 0) {                                                  \
            return try_err_;                                                  \
        }                                                                     \
    } while (0)

static int
decode_server(const cJSON *p, struct oasp_server *out)
{
    TRY(json_get_text(p, "Name", out->name, sizeof(out->name)));
    TRY(get_int(p, "Rows", &out->rows));
    TRY(get_int(p, "Columns", &out->columns));
    TRY(json_get_bool(p, "TXRX", &out->txrx));
    return 0;
}

static int
decode_rotators(const cJSON *p, struct oasp_rotators *out)
{
    const cJSON *arr;
    /* Not decoded yet, but still bounded like every other array. */
    TRY(json_get_array(p, "Rotators", CAPS_OASP_ANTENNAS, &arr, &out->count));
    return 0;
}

/* Allocates count zeroed elements for ptr; ptr stays NULL for count 0. */
#define ALLOC_ARRAY(ptr, count)                                               \
    do {                                                                      \
        if ((count) > 0) {                                                    \
            (ptr) = calloc((count), sizeof(*(ptr)));                          \
            if ((ptr) == NULL) {                                              \
                return ERR_NOMEM;                                             \
            }                                                                 \
        }                                                                     \
    } while (0)

static int
decode_description(const cJSON *p, struct oasp_antenna_description *out)
{
    if (!cJSON_IsObject(p)) {
        return ERR_INVALID;
    }
    TRY(json_get_text(p, "Manufacturer", out->manufacturer,
                      sizeof(out->manufacturer)));
    TRY(json_get_text(p, "Model", out->model, sizeof(out->model)));
    TRY(json_get_text(p, "Type", out->type, sizeof(out->type)));
    TRY(get_int(p, "Elements", &out->elements));
    TRY(json_get_double(p, "3dBWidth", &out->width_3db));
    TRY(get_int(p, "Lobes", &out->lobes));
    TRY(json_get_double(p, "Direction", &out->direction));
    return 0;
}

static int
decode_antenna_button(const cJSON *p, struct oasp_antenna_button *out)
{
    if (!cJSON_IsObject(p)) {
        return ERR_INVALID;
    }
    TRY(get_int(p, "Antenna", &out->antenna));
    TRY(json_get_text(p, "ShortName", out->short_name,
                      sizeof(out->short_name)));
    TRY(json_get_text(p, "LongName", out->long_name, sizeof(out->long_name)));
    TRY(json_get_bool(p, "Additive", &out->additive));
    TRY(get_int(p, "Row", &out->row));
    TRY(get_int(p, "Column", &out->column));

    const cJSON *arr;
    TRY(json_get_array(p, "Description", CAPS_OASP_DESCRIPTIONS, &arr,
                       &out->description_count));
    size_t i = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, arr)
    {
        TRY(decode_description(item, &out->descriptions[i++]));
    }
    return 0;
}

static int
decode_function_button(const cJSON *p, struct oasp_function_button *out)
{
    if (!cJSON_IsObject(p)) {
        return ERR_INVALID;
    }
    TRY(get_int(p, "Function", &out->function));
    TRY(json_get_text(p, "ShortName", out->short_name,
                      sizeof(out->short_name)));
    TRY(json_get_text(p, "LongName", out->long_name, sizeof(out->long_name)));
    TRY(json_get_bool(p, "Momentary", &out->momentary));
    TRY(json_get_bool(p, "Alternate", &out->alternate));
    TRY(get_int(p, "Row", &out->row));
    TRY(get_int(p, "Column", &out->column));
    return 0;
}

static int
decode_configuration(const cJSON *p, struct oasp_configuration *out)
{
    TRY(get_int(p, "Station", &out->station));
    TRY(get_int(p, "Radio", &out->radio));
    TRY(json_get_double(p, "LowerFrequency", &out->lower_khz));
    TRY(json_get_double(p, "UpperFrequency", &out->upper_khz));

    const cJSON *arr;
    const cJSON *item;
    size_t count;
    size_t i;

    TRY(json_get_array(p, "Antennas", CAPS_OASP_ANTENNAS, &arr, &count));
    ALLOC_ARRAY(out->antennas, count);
    out->antenna_count = count;
    i = 0;
    cJSON_ArrayForEach(item, arr)
    {
        TRY(decode_antenna_button(item, &out->antennas[i++]));
    }

    TRY(json_get_array(p, "Functions", CAPS_OASP_FUNCTIONS, &arr, &count));
    ALLOC_ARRAY(out->functions, count);
    out->function_count = count;
    i = 0;
    cJSON_ArrayForEach(item, arr)
    {
        TRY(decode_function_button(item, &out->functions[i++]));
    }
    return 0;
}

static int
decode_antenna_state(const cJSON *p, struct oasp_antenna_state *out)
{
    if (!cJSON_IsObject(p)) {
        return ERR_INVALID;
    }
    TRY(get_int(p, "Antenna", &out->antenna));
    TRY(get_int(p, "TX", &out->tx));
    TRY(get_int(p, "RX", &out->rx));
    return 0;
}

static int
decode_function_state(const cJSON *p, struct oasp_function_state *out)
{
    if (!cJSON_IsObject(p)) {
        return ERR_INVALID;
    }
    TRY(get_int(p, "Function", &out->function));
    TRY(get_int(p, "State", &out->state));
    return 0;
}

static int
decode_state(const cJSON *p, struct oasp_state *out)
{
    TRY(get_int(p, "Station", &out->station));
    TRY(get_int(p, "Radio", &out->radio));
    TRY(json_get_text(p, "TXShortName", out->tx_short_name,
                      sizeof(out->tx_short_name)));
    TRY(json_get_text(p, "TXLongName", out->tx_long_name,
                      sizeof(out->tx_long_name)));
    TRY(get_int(p, "TXId", &out->tx_id));
    TRY(json_get_text(p, "RXShortName", out->rx_short_name,
                      sizeof(out->rx_short_name)));
    TRY(json_get_text(p, "RXLongName", out->rx_long_name,
                      sizeof(out->rx_long_name)));
    TRY(get_int(p, "RXId", &out->rx_id));

    const cJSON *arr;
    const cJSON *item;
    size_t count;
    size_t i;

    TRY(json_get_array(p, "Antennas", CAPS_OASP_ANTENNAS, &arr, &count));
    ALLOC_ARRAY(out->antennas, count);
    out->antenna_count = count;
    i = 0;
    cJSON_ArrayForEach(item, arr)
    {
        TRY(decode_antenna_state(item, &out->antennas[i++]));
    }

    TRY(json_get_array(p, "Functions", CAPS_OASP_FUNCTIONS, &arr, &count));
    ALLOC_ARRAY(out->functions, count);
    out->function_count = count;
    i = 0;
    cJSON_ArrayForEach(item, arr)
    {
        TRY(decode_function_state(item, &out->functions[i++]));
    }
    return 0;
}

static int
decode_tr(const cJSON *p, struct oasp_tr *out)
{
    TRY(get_int(p, "Station", &out->station));
    TRY(get_int(p, "Radio", &out->radio));
    TRY(get_int(p, "TR", &out->tr));
    return 0;
}

static int
decode_text(const cJSON *p, struct oasp_text *out)
{
    TRY(get_int(p, "Action", &out->action));
    TRY(json_get_text(p, "Text", out->text, sizeof(out->text)));
    return 0;
}

static int
decode_logger(const cJSON *p, struct oasp_logger *out)
{
    TRY(get_int(p, "Station", &out->station));
    TRY(get_int(p, "Radio", &out->radio));
    TRY(json_get_text(p, "Message", out->message, sizeof(out->message)));
    return 0;
}

static int
decode_unknown(const cJSON *p, struct oasp_unknown *out)
{
    if (p == NULL) {
        return 0;
    }
    out->parameters = cJSON_PrintUnformatted(p);
    return out->parameters == NULL ? ERR_NOMEM : 0;
}

/* Frees what a message owns, whatever its kind, and empties it. */
static void
free_parts(struct oasp_message *msg)
{
    switch (msg->kind) {
    case OASP_CONFIGURATION:
        free(msg->u.configuration.antennas);
        free(msg->u.configuration.functions);
        break;
    case OASP_STATE:
        free(msg->u.state.antennas);
        free(msg->u.state.functions);
        break;
    case OASP_UNKNOWN:
        cJSON_free(msg->u.unknown.parameters);
        break;
    default:
        break;
    }
    memset(&msg->u, 0, sizeof(msg->u));
    msg->kind = OASP_NONE;
}

int
oasp_decode(struct str json, struct oasp_message *msg)
{
    memset(msg, 0, sizeof(*msg));

    cJSON *root;
    if (json_parse(json, &root) != 0) {
        return ERR_INVALID;
    }
    int err = ERR_INVALID;
    const cJSON *params = NULL;
    if (!cJSON_IsObject(root) ||
        json_get_text(root, "Message", msg->name, sizeof(msg->name)) != 0 ||
        msg->name[0] == '\0' ||
        json_get_int(root, "Sequence", 0, INT_MAX, &msg->sequence) != 0 ||
        json_get_object(root, "Parameters", &params) != 0)
    {
        msg->name[0] = '\0';
        goto out;
    }

    /*
     * Set the kind first, so that free_parts() knows what to free if the
     * parameters turn out to be bad half way through.
     */
    if (strcmp(msg->name, "Server") == 0) {
        msg->kind = OASP_SERVER;
        err = decode_server(params, &msg->u.server);
    } else if (strcmp(msg->name, "Rotators") == 0) {
        msg->kind = OASP_ROTATORS;
        err = decode_rotators(params, &msg->u.rotators);
    } else if (strcmp(msg->name, "Configuration") == 0) {
        msg->kind = OASP_CONFIGURATION;
        err = decode_configuration(params, &msg->u.configuration);
    } else if (strcmp(msg->name, "State") == 0) {
        msg->kind = OASP_STATE;
        err = decode_state(params, &msg->u.state);
    } else if (strcmp(msg->name, "TR") == 0) {
        msg->kind = OASP_TR;
        err = decode_tr(params, &msg->u.tr);
    } else if (strcmp(msg->name, "Text") == 0) {
        msg->kind = OASP_TEXT;
        err = decode_text(params, &msg->u.text);
    } else if (strcmp(msg->name, "Logger") == 0) {
        msg->kind = OASP_LOGGER;
        err = decode_logger(params, &msg->u.logger);
    } else {
        msg->kind = OASP_UNKNOWN;
        err = decode_unknown(params, &msg->u.unknown);
    }
    if (err != 0) {
        free_parts(msg);
    }

out:
    cJSON_Delete(root);
    return err;
}

void
oasp_message_free(struct oasp_message *msg)
{
    free_parts(msg);
}

void
oasp_message_take(struct oasp_message *dst, struct oasp_message *src)
{
    if (dst == src) {
        return;
    }
    free_parts(dst);
    *dst = *src;
    memset(src, 0, sizeof(*src));
    src->kind = OASP_NONE;
}

/*
 * Encoding.  cJSON keeps members in the order they are added, which is the
 * order the server's own client sends them in.
 */

static bool
add_int(cJSON *obj, const char *key, int value)
{
    return cJSON_AddNumberToObject(obj, key, value) != NULL;
}

/*
 * Starts a message: the envelope, with an empty Parameters object returned
 * in *params.
 */
static int
begin(const char *name, int sequence, cJSON **root, cJSON **params)
{
    *root = cJSON_CreateObject();
    if (*root == NULL) {
        return ERR_NOMEM;
    }
    if (cJSON_AddStringToObject(*root, "Message", name) == NULL ||
        !add_int(*root, "Sequence", sequence) ||
        !add_int(*root, "Version", OASP_VERSION) ||
        (*params = cJSON_AddObjectToObject(*root, "Parameters")) == NULL)
    {
        cJSON_Delete(*root);
        return ERR_NOMEM;
    }
    return 0;
}

/* Writes the message out and frees it; ERR_NOMEM if building it failed. */
static int
finish(struct buf *out, cJSON *root, bool ok)
{
    int err = ok ? json_write(out, root) : ERR_NOMEM;
    cJSON_Delete(root);
    return err;
}

static bool
add_radio_id(cJSON *obj, struct oasp_radio_id id)
{
    return add_int(obj, "Station", id.station) &&
           add_int(obj, "Radio", id.radio);
}

int
oasp_encode_hello(struct buf *out, int sequence,
                  const struct oasp_hello *hello)
{
    if (hello->radio_count > CAPS_OASP_RADIOS || hello->name == NULL) {
        return ERR_INVALID;
    }
    cJSON *root;
    cJSON *p;
    int err = begin("Hello", sequence, &root, &p);
    if (err != 0) {
        return err;
    }
    bool ok = cJSON_AddStringToObject(p, "Protocol", OASP_PROTOCOL) != NULL &&
              cJSON_AddStringToObject(p, "Name", hello->name) != NULL &&
              cJSON_AddBoolToObject(p, "Bidirectional",
                                    hello->bidirectional) != NULL;
    /* IndicateTR is left out unless it is true. */
    if (ok && hello->indicate_tr) {
        ok = cJSON_AddTrueToObject(p, "IndicateTR") != NULL;
    }
    cJSON *radios = ok ? cJSON_AddArrayToObject(p, "Radios") : NULL;
    ok = radios != NULL;
    for (size_t i = 0; ok && i < hello->radio_count; i++) {
        cJSON *r = cJSON_CreateObject();
        ok = r != NULL && cJSON_AddItemToArray(radios, r) &&
             add_radio_id(r, hello->radios[i]);
    }
    ok = ok && add_int(p, "Rows", hello->rows) &&
         add_int(p, "Columns", hello->columns);
    return finish(out, root, ok);
}

int
oasp_encode_radio(struct buf *out, int sequence, struct oasp_radio_id id,
                  int khz)
{
    cJSON *root;
    cJSON *p;
    int err = begin("Radio", sequence, &root, &p);
    if (err != 0) {
        return err;
    }
    bool ok = add_radio_id(p, id) && add_int(p, "Frequency", khz);
    return finish(out, root, ok);
}

/* One entry of an Antenna request: {"TX":change,"Antenna":id}, or RX. */
static bool
add_choice(cJSON *arr, const char *side, int change, int antenna)
{
    cJSON *c = cJSON_CreateObject();
    return c != NULL && cJSON_AddItemToArray(arr, c) &&
           add_int(c, side, change) && add_int(c, "Antenna", antenna);
}

static bool
valid_change(int change)
{
    return (change >= OASP_CHANGE_NONE && change <= OASP_CHANGE_PREVIOUS) ||
           change == OASP_CHANGE_SAME_AS_TX;
}

int
oasp_encode_antenna(struct buf *out, int sequence, struct oasp_radio_id id,
                    int antenna, int tx_change, int rx_change)
{
    bool tx = tx_change != OASP_CHANGE_NONE;
    bool rx = rx_change != OASP_CHANGE_NONE;
    if ((!tx && !rx) || !valid_change(tx_change) || !valid_change(rx_change) ||
        tx_change == OASP_CHANGE_SAME_AS_TX)
    {
        return ERR_INVALID;
    }
    cJSON *root;
    cJSON *p;
    int err = begin("Antenna", sequence, &root, &p);
    if (err != 0) {
        return err;
    }
    bool ok = add_radio_id(p, id);
    cJSON *arr = ok ? cJSON_AddArrayToObject(p, "Antennas") : NULL;
    ok = arr != NULL;
    if (ok && tx) {
        ok = add_choice(arr, "TX", tx_change, antenna);
    }
    if (ok && rx) {
        ok = add_choice(arr, "RX", rx_change, antenna);
    }
    return finish(out, root, ok);
}

int
oasp_encode_function(struct buf *out, int sequence, struct oasp_radio_id id,
                     int function, int change)
{
    if (change < OASP_FUNCTION_SELECT || change > OASP_FUNCTION_ALTERNATE) {
        return ERR_INVALID;
    }
    cJSON *root;
    cJSON *p;
    int err = begin("Function", sequence, &root, &p);
    if (err != 0) {
        return err;
    }
    bool ok = add_radio_id(p, id) && add_int(p, "Function", function) &&
              add_int(p, "State", change);
    return finish(out, root, ok);
}

int
oasp_encode_logger(struct buf *out, int sequence, struct oasp_radio_id id,
                   const char *message)
{
    size_t len = message != NULL ? strlen(message) : 0;
    if (message == NULL || len >= CAPS_OASP_NOTICE) {
        return ERR_INVALID;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)message[i];
        if (ch < 0x20 || ch > 0x7e) {
            return ERR_INVALID; /* printable ASCII only */
        }
    }
    cJSON *root;
    cJSON *p;
    int err = begin("Logger", sequence, &root, &p);
    if (err != 0) {
        return err;
    }
    bool ok = add_radio_id(p, id) &&
              cJSON_AddStringToObject(p, "Message", message) != NULL;
    return finish(out, root, ok);
}
