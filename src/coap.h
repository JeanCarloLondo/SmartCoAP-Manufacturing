#ifndef COAP_H
#define COAP_H

#include <stdint.h>
#include <stddef.h>

// CoAP constants
#define COAP_VERSION 1
#define COAP_MAX_TOKEN_LEN 8
#define COAP_PAYLOAD_MARKER 0xFF

// Types
typedef enum
{
    COAP_TYPE_CON = 0,
    COAP_TYPE_NON = 1,
    COAP_TYPE_ACK = 2,
    COAP_TYPE_RST = 3
} coap_type_t;

// Methods (class 0) and simple mapping (useful helpers)
#define COAP_METHOD_EMPTY 0
#define COAP_METHOD_GET 1
#define COAP_METHOD_POST 2
#define COAP_METHOD_PUT 3
#define COAP_METHOD_DELETE 4

// Parse/Serialize return codes
typedef enum
{
    COAP_OK = 0,
    COAP_ERR_INVALID = -1,
    COAP_ERR_TRUNCATED = -2,
    COAP_ERR_TKL_TOO_LARGE = -3,
    COAP_ERR_OPTIONS_NOT_SUPPORTED = -4,
    COAP_ERR_VERSION_MISMATCH = -5
} coap_status_t;

// CoAP message structure (simplified, no Options)
typedef struct
{
    uint8_t version;                   // 2 bits used (value 1)
    coap_type_t type;                  // 2 bits
    uint8_t tkl;                       // token length 0..8
    uint8_t code;                      // method or response code
    uint16_t message_id;               // message ID (network order handled in serialize/parse)
    uint8_t token[COAP_MAX_TOKEN_LEN]; // token bytes if tkl>0
    uint8_t *payload;                  // pointer to payload bytes (NULL if none)
    size_t payload_len;                // length of payload
} coap_message_t;

/*
 * Serialize a coap_message_t into a caller-provided buffer.
 * Returns number of bytes written on success (>0), or negative coap_status_t on error.
 * The buffer must be large enough; recommended size >= 4 + tkl + 1 + payload_len
 */
int coap_serialize(const coap_message_t *msg, uint8_t *out_buf, size_t out_buf_len);

/*
 * Parse bytes into coap_message_t.
 * On success returns COAP_OK and fills msg. Caller is responsible for freeing msg->payload if non-NULL.
 * If the version differs from COAP_VERSION, returned status is COAP_ERR_VERSION_MISMATCH but msg
 * will contain parsed message_id (useful to craft RST).
 */
int coap_parse(const uint8_t *buf, size_t buf_len, coap_message_t *msg);

/*
 * Free resources inside a parsed message (frees payload if allocated).
 */
void coap_free_message(coap_message_t *msg);

/*
 * Convenience constructors:
 * - build_empty_ack: creates an ACK message (empty code 0.00) that echoes the message_id of req.
 * - build_rst_for: creates a RST message for req (empty).
 *
 * The returned coap_message_t has payload == NULL. Caller may pass it to coap_serialize() or free it.
 */
void coap_build_empty_ack(const coap_message_t *req, coap_message_t *out_ack);
void coap_build_rst_for(const coap_message_t *req, coap_message_t *out_rst);

/*
 * Utility to initialize a message to safe defaults.
 */
void coap_init_message(coap_message_t *msg);

// CoAP Method Codes (class 0)
#define COAP_CODE_EMPTY 0x00
#define COAP_CODE_GET 0x01
#define COAP_CODE_POST 0x02
#define COAP_CODE_PUT 0x03
#define COAP_CODE_DELETE 0x04

// CoAP Success Response Codes (class 2)
#define COAP_CODE_CREATED 0x41 // 2.01
#define COAP_CODE_DELETED 0x42 // 2.02
#define COAP_CODE_VALID 0x43   // 2.03
#define COAP_CODE_CHANGED 0x44 // 2.04
#define COAP_CODE_CONTENT 0x45 // 2.05

// CoAP Client Error Response Codes (class 4)
#define COAP_CODE_BAD_REQUEST 0x80 // 4.00
#define COAP_CODE_NOT_FOUND 0x84   // 4.04

// CoAP Server Error Response Codes (class 5)
#define COAP_CODE_INTERNAL_ERROR 0xA0 // 5.00

#endif // COAP_H