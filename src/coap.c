#include "coap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h> // htons/ntohs for Windows
#else
#include <arpa/inet.h> // htons/ntohs for POSIX
#endif

// ==========================
// Initialization
// ==========================
// Initialize a CoAP message structure with default values
void coap_init_message(coap_message_t *msg)
{
    if (!msg)
        return;
    msg->version = COAP_VERSION;  // CoAP protocol version (always 1)
    msg->type = COAP_TYPE_CON;    // Default type: Confirmable
    msg->tkl = 0;                 // Token length (0 = no token)
    msg->code = COAP_METHOD_EMPTY; // Default code is 0.00 (empty)
    msg->message_id = 0;          // Message ID to be set later
    memset(msg->token, 0, sizeof msg->token);

    msg->options = NULL;         // No options initially
    msg->options_count = 0;

    msg->payload = NULL;         // No payload initially
    msg->payload_len = 0;
}

// ==========================
// Serialization
// ==========================
// Convert a CoAP message structure into a byte buffer (wire format)
int coap_serialize(const coap_message_t *msg, uint8_t *out_buf, size_t out_buf_len)
{
    if (!msg || !out_buf)
        return COAP_ERR_INVALID;
    if (msg->tkl > COAP_MAX_TOKEN_LEN)
        return COAP_ERR_TKL_TOO_LARGE;

    // Estimate required buffer size
    size_t needed = 4 + msg->tkl;
    for (size_t i = 0; i < msg->options_count; i++)
    {
        needed += 1 + msg->options[i].length;  
    }
    if (msg->payload_len)
    {
        needed += 1 + msg->payload_len;  // add marker + payload
    }
    if (out_buf_len < needed)
        return COAP_ERR_TRUNCATED;

    // First byte: version, type, token length
    uint8_t first = ((msg->version & 0x03) << 6) |
                    (((uint8_t)msg->type & 0x03) << 4) |
                    (msg->tkl & 0x0F);

    // Fill header

    // second byte is code
    out_buf[0] = first;
    out_buf[1] = msg->code;

    // third and fourth bytes are message ID
    uint16_t mid_net = htons(msg->message_id);
    out_buf[2] = (uint8_t)((mid_net >> 8) & 0xFF);
    out_buf[3] = (uint8_t)(mid_net & 0xFF);

    size_t idx = 4;

    // token
    if (msg->tkl)
    {
        memcpy(out_buf + idx, msg->token, msg->tkl);
        idx += msg->tkl;
    }

    // Serialize options (delta encoding simplified: only small deltas allowed)
    uint16_t running_delta = 0;
    for (size_t i = 0; i < msg->options_count; i++)
    {
        const coap_option_t *opt = &msg->options[i];
        uint16_t option_delta = opt->number - running_delta;
        running_delta = opt->number;

        if (opt->length > 15 || option_delta > 15)
        {
            return COAP_ERR_OPTIONS_NOT_SUPPORTED;
        }

        uint8_t header = (uint8_t)((option_delta << 4) | (opt->length & 0x0F));
        out_buf[idx++] = header;
        memcpy(out_buf + idx, opt->value, opt->length);
        idx += opt->length;
    }

    // Payload (if present)
    if (msg->payload_len)
    {
        out_buf[idx++] = COAP_PAYLOAD_MARKER; // 0xFF marker
        memcpy(out_buf + idx, msg->payload, msg->payload_len);
        idx += msg->payload_len;
    }
    return (int)idx;
}

// ==========================
// Parsing
// ==========================
// Parse a raw byte buffer into a CoAP message structure
int coap_parse(const uint8_t *buf, size_t buf_len, coap_message_t *msg)
{
    if (!buf || !msg)
        return COAP_ERR_INVALID;
    if (buf_len < 4)
        return COAP_ERR_TRUNCATED;

    size_t idx = 0;
    uint8_t first = buf[idx++];

    uint8_t version = (first >> 6) & 0x03;
    uint8_t type = (first >> 4) & 0x03;
    uint8_t tkl = first & 0x0F;

    uint8_t code = buf[idx++];
    uint16_t mid_net = ((uint16_t)buf[idx++] << 8);
    mid_net |= (uint16_t)buf[idx++];

    // Fill message fields
    msg->version = version;
    msg->type = (coap_type_t)(type & 0x03);
    msg->tkl = tkl;
    msg->code = code;
    msg->message_id = ((uint16_t)buf[2] << 8) | (uint16_t)buf[3];
    memset(msg->token, 0, sizeof msg->token);
    msg->payload = NULL;
    msg->payload_len = 0;
    msg->options = NULL;
    msg->options_count = 0;

    if (version != COAP_VERSION)
        return COAP_ERR_VERSION_MISMATCH;

    // Token
    if (tkl > COAP_MAX_TOKEN_LEN)
        return COAP_ERR_TKL_TOO_LARGE;
    if (idx + tkl > buf_len)
        return COAP_ERR_TRUNCATED;
    if (tkl)
        memcpy(msg->token, buf + idx, tkl);
    idx += tkl;

    // Parse options and payload
    uint16_t running_delta = 0;
    while (idx < buf_len)
    {
        if (buf[idx] == COAP_PAYLOAD_MARKER)
        {
            // Payload starts after marker
            idx++;
            if (idx >= buf_len)
                return COAP_ERR_TRUNCATED;
            msg->payload_len = buf_len - idx;
            msg->payload = (uint8_t *)malloc(msg->payload_len);
            if (!msg->payload)
                return COAP_ERR_INVALID;
            memcpy(msg->payload, buf + idx, msg->payload_len);
            return COAP_OK;
        }

        // Option header
        uint8_t byte = buf[idx++];
        uint8_t opt_delta = (byte >> 4) & 0x0F;
        uint8_t opt_len = (byte & 0x0F);

        if (opt_delta == 15 || opt_len == 15)
        {
            return COAP_ERR_OPTIONS_NOT_SUPPORTED;
        }
        if (idx + opt_len > buf_len)    
            return COAP_ERR_TRUNCATED;

        uint16_t opt_num = running_delta + opt_delta;
        running_delta = opt_num;

        // Add new option to the message (expand array)
        coap_option_t *tmp = realloc(msg->options, (msg->options_count + 1) * sizeof(coap_option_t));
        if (!tmp)
            return COAP_ERR_INVALID;
        msg->options = tmp;

        coap_option_t *opt = &msg->options[msg->options_count++];
        opt->number = opt_num;
        opt->length = opt_len;
        opt->value = (uint8_t *)malloc(opt_len);
        if (!opt->value)
            return COAP_ERR_INVALID;
        memcpy(opt->value, buf + idx, opt_len);
        idx += opt_len;
    }

    return COAP_OK;
}

// ==========================
// Free resources
// ==========================
// Free dynamically allocated memory in a CoAP message (options, payload)
void coap_free_message(coap_message_t *msg)
{
    if (!msg)
        return;

    // Free options
    if (msg->options)
    {
        for (size_t i = 0; i < msg->options_count; i++)
        {
            if (msg->options[i].value)
            {
                free(msg->options[i].value);
                msg->options[i].value = NULL;
            }
        }
        free(msg->options);
        msg->options = NULL;
        msg->options_count = 0;
    }

    // Free payload
    if (msg->payload)
    {
        free(msg->payload);
        msg->payload = NULL;
        msg->payload_len = 0;
    }
}

// ==========================
// Build empty ACK
// ==========================
// Create an empty ACK message in response to a Confirmable request
void coap_build_empty_ack(const coap_message_t *req, coap_message_t *ack)
{
    coap_init_message(ack);
    ack->type = COAP_TYPE_ACK;
    ack->code = COAP_METHOD_EMPTY;
    ack->message_id = req->message_id;
}

// ==========================
// Build RST
// ==========================
// Create a Reset (RST) message in response to an invalid or unexpected message
void coap_build_rst_for(const coap_message_t *req, coap_message_t *rst)
{
    coap_init_message(rst);
    rst->type = COAP_TYPE_RST;
    rst->code = COAP_METHOD_EMPTY;
    rst->message_id = req->message_id;
}

// ==========================
// Add option
// ==========================
// Insert a new option into the CoAP message, keeping options sorted by number
int coap_add_option(coap_message_t *msg, uint16_t number, const uint8_t *value, size_t length)
{
    if (!msg)
        return COAP_ERR_INVALID;
    if (!value && length > 0)
        return COAP_ERR_INVALID;
    if (length > 15)  // this implementation supports only small options
        return COAP_ERR_OPTION_OVERSIZE;

    // Find insertion point to keep options sorted by number
    size_t idx = 0;
    for (; idx < msg->options_count; ++idx)
    {
        if (msg->options[idx].number > number)
            break;
    }

    // Reallocate memory for one more option
    coap_option_t *tmp = (coap_option_t *)realloc(msg->options, (msg->options_count + 1) * sizeof(coap_option_t));
    if (!tmp)
        return COAP_ERR_INVALID;
    msg->options = tmp;

    // Shift existing options to make space
    for (size_t j = msg->options_count; j > idx; --j)
    {
        msg->options[j] = msg->options[j - 1];
    }

    // Initialize new option slot
    msg->options[idx].number = number;
    msg->options[idx].length = (uint16_t)length;
    msg->options[idx].value = NULL;

    if (length > 0)
    {
        msg->options[idx].value = (uint8_t *)malloc(length);
        if (!msg->options[idx].value)
        {
            // Rollback if malloc fails
            for (size_t j = idx; j < msg->options_count; ++j)
                msg->options[j] = msg->options[j + 1];
            // reduce size
            if (msg->options_count == 0)
            {
                free(msg->options);
                msg->options = NULL;
            }
            else
            {
                coap_option_t *tmp2 = (coap_option_t *)realloc(msg->options, msg->options_count * sizeof(coap_option_t));
                if (tmp2)
                    msg->options = tmp2;
            }
            return COAP_ERR_INVALID;
        }
        memcpy(msg->options[idx].value, value, length);
    }
    msg->options_count++;
    return COAP_OK;
}