#include "coap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h> // htons/ntohs for Windows
#else
#include <arpa/inet.h> // htons/ntohs for POSIX
#endif

void coap_init_message(coap_message_t *msg) {
    if (!msg) return;
    msg->version = COAP_VERSION;
    msg->type = COAP_TYPE_CON;
    msg->tkl = 0;
    msg->code = COAP_METHOD_EMPTY;
    msg->message_id = 0;
    memset(msg->token, 0, COAP_MAX_TOKEN_LEN);
    msg->payload = NULL;
    msg->payload_len = 0;
}

int coap_serialize(const coap_message_t *msg, uint8_t *out_buf, size_t out_buf_len) {
    if (!msg || !out_buf) return COAP_ERR_INVALID;
    if (msg->tkl > COAP_MAX_TOKEN_LEN) return COAP_ERR_TKL_TOO_LARGE;

    // Minimum header size
    size_t needed = 4 + msg->tkl + (msg->payload_len ? 1 + msg->payload_len : 0);
    if (out_buf_len < needed) return COAP_ERR_TRUNCATED;

    // First byte: Version (2 bits) | Type (2 bits) | TKL (4 bits)
    uint8_t first = ((msg->version & 0x03) << 6) | (( (uint8_t)msg->type & 0x03) << 4) | (msg->tkl & 0x0F);
    out_buf[0] = first;
    // Code
    out_buf[1] = msg->code;
    // Message ID (network byte order)
    uint16_t mid_net = htons(msg->message_id);
    out_buf[2] = (uint8_t)((mid_net >> 8) & 0xFF);
    out_buf[3] = (uint8_t)(mid_net & 0xFF);

    size_t idx = 4;
    // Token if any
    if (msg->tkl) {
        memcpy(out_buf + idx, msg->token, msg->tkl);
        idx += msg->tkl;
    }

    // Options: NOT SUPPORTED (project rule). If any non-empty options exist in a message we won't serialize them.

    // Payload marker + payload
    if (msg->payload_len) {
        out_buf[idx++] = COAP_PAYLOAD_MARKER;
        memcpy(out_buf + idx, msg->payload, msg->payload_len);
        idx += msg->payload_len;
    }

    return (int)idx;
}

int coap_parse(const uint8_t *buf, size_t buf_len, coap_message_t *msg) {
    if (!buf || !msg) return COAP_ERR_INVALID;
    if (buf_len < 4) return COAP_ERR_TRUNCATED;

    size_t idx = 0;
    uint8_t first = buf[idx++];

    uint8_t version = (first >> 6) & 0x03;
    uint8_t type    = (first >> 4) & 0x03;
    uint8_t tkl     = first & 0x0F;

    uint8_t code = buf[idx++];
    uint16_t mid_net = ( (uint16_t)buf[idx++] << 8 );
    mid_net |= (uint16_t)buf[idx++];

    // Fill basic fields
    msg->version = version;
    msg->type = (coap_type_t)(type & 0x03);
    msg->tkl = tkl;
    msg->code = code;
    msg->message_id = ntohs(mid_net);
    memset(msg->token, 0, COAP_MAX_TOKEN_LEN);
    msg->payload = NULL;
    msg->payload_len = 0;

    if (version != COAP_VERSION) {
        // Return version mismatch; but msg contains parsed MID for forming RST if needed.
        return COAP_ERR_VERSION_MISMATCH;
    }

    if (tkl > COAP_MAX_TOKEN_LEN) return COAP_ERR_TKL_TOO_LARGE;
    // token bytes present?
    if (tkl) {
        if (buf_len < idx + tkl) return COAP_ERR_TRUNCATED;
        memcpy(msg->token, buf + idx, tkl);
        idx += tkl;
    }

    // After token we expect either payload marker (0xFF) or end of buffer
    if (idx < buf_len) {
        // If next byte is 0xFF -> payload present
        if (buf[idx] == COAP_PAYLOAD_MARKER) {
            idx++;
            size_t remaining = buf_len - idx;
            if (remaining > 0) {
                msg->payload = (uint8_t*) malloc(remaining);
                if (!msg->payload) return COAP_ERR_INVALID;
                memcpy(msg->payload, buf + idx, remaining);
                msg->payload_len = remaining;
                idx += remaining;
            } else {
                // Payload marker present but no bytes after -> payload_len = 0 (empty payload)
                msg->payload = NULL;
                msg->payload_len = 0;
            }
        } else {
            // There are non-payload bytes after token -> options would exist, but options not supported
            return COAP_ERR_OPTIONS_NOT_SUPPORTED;
        }
    }
    return COAP_OK;
}

void coap_free_message(coap_message_t *msg) {
    if (!msg) return;
    if (msg->payload) {
        free(msg->payload);
        msg->payload = NULL;
        msg->payload_len = 0;
    }
}

void coap_build_empty_ack(const coap_message_t *req, coap_message_t *out_ack) {
    coap_init_message(out_ack);
    out_ack->type = COAP_TYPE_ACK;
    out_ack->tkl = 0;
    out_ack->code = 0; // Empty ACK (0.00)
    out_ack->message_id = req->message_id;
    // token omitted in empty ack (tkl = 0)
}

void coap_build_rst_for(const coap_message_t *req, coap_message_t *out_rst) {
    coap_init_message(out_rst);
    out_rst->type = COAP_TYPE_RST;
    out_rst->tkl = 0;
    out_rst->code = 0;
    out_rst->message_id = req->message_id;
}