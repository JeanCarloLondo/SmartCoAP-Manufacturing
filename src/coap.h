#ifndef COAP_H
#define COAP_H

#include <stdint.h>
#include <stddef.h>

// ==========================
// CoAP constants
// ==========================
#define COAP_VERSION 1
#define COAP_MAX_TOKEN_LEN 8
#define COAP_PAYLOAD_MARKER 0xFF

// ==========================
// Types
// ==========================
typedef enum
{
    COAP_TYPE_CON = 0,
    COAP_TYPE_NON = 1,
    COAP_TYPE_ACK = 2,
    COAP_TYPE_RST = 3
} coap_type_t;

// Methods (class 0)
#define COAP_METHOD_EMPTY 0
#define COAP_METHOD_GET 1
#define COAP_METHOD_POST 2
#define COAP_METHOD_PUT 3
#define COAP_METHOD_DELETE 4

// ==========================
// Parse/Serialize return codes
// ==========================
typedef enum
{
    COAP_OK = 0,
    COAP_ERR_INVALID = -1,
    COAP_ERR_TRUNCATED = -2,
    COAP_ERR_TKL_TOO_LARGE = -3,
    COAP_ERR_OPTIONS_NOT_SUPPORTED = -4,
    COAP_ERR_VERSION_MISMATCH = -5,
    COAP_ERR_OPTION_OVERSIZE = -6
} coap_status_t;

// ==========================
// Option structure
// ==========================
typedef struct
{
    uint16_t number; // option number
    uint16_t length; // option length
    uint8_t *value;  // buffer for option value
} coap_option_t;

// ==========================
// CoAP message structure
// ==========================
typedef struct
{
    uint8_t version;
    coap_type_t type;
    uint8_t tkl;
    uint8_t code;
    uint16_t message_id;
    uint8_t token[COAP_MAX_TOKEN_LEN];

    coap_option_t *options; // dinamic 
    size_t options_count;

    uint8_t *payload; // dinamic
    size_t payload_len;
} coap_message_t;

// ==========================
// Function prototypes
// ==========================

/*
 * Serialize a coap_message_t into a caller-provided buffer.
 * Returns number of bytes written on success (>0), or negative coap_status_t on error.
 */
int coap_serialize(const coap_message_t *msg, uint8_t *out_buf, size_t out_buf_len);

/*
 * Parse bytes into coap_message_t.
 * On success returns COAP_OK and fills msg. Caller is responsible for freeing msg->payload if non-NULL.
 * If the version differs from COAP_VERSION, returns COAP_ERR_VERSION_MISMATCH.
 */
int coap_parse(const uint8_t *buf, size_t buf_len, coap_message_t *msg);

/*
 * Free resources inside a parsed message (frees payload if allocated).
 */
void coap_free_message(coap_message_t *msg);

/*
 * Convenience constructors
 */
void coap_build_empty_ack(const coap_message_t *req, coap_message_t *out_ack);
void coap_build_rst_for(const coap_message_t *req, coap_message_t *out_rst);

/*
 * Utility to initialize a message to safe defaults.
 */
void coap_init_message(coap_message_t *msg);

/*
 * Add an option to a coap_message_t.
 * The option list is kept ordered by option number (ascending).
 * Returns COAP_OK (0) on success or a negative coap_status_t on error.
 */
int coap_add_option(coap_message_t *msg, uint16_t number, const uint8_t *value, size_t length);

// ==========================
// CoAP Codes
// ==========================
// Method Codes (class 0)
#define COAP_CODE_EMPTY 0x00
#define COAP_CODE_GET 0x01
#define COAP_CODE_POST 0x02
#define COAP_CODE_PUT 0x03
#define COAP_CODE_DELETE 0x04

// Success (class 2)
#define COAP_CODE_CREATED 0x41 // 2.01
#define COAP_CODE_DELETED 0x42 // 2.02
#define COAP_CODE_VALID 0x43   // 2.03
#define COAP_CODE_CHANGED 0x44 // 2.04
#define COAP_CODE_CONTENT 0x45 // 2.05

// Client Error (class 4)
#define COAP_CODE_BAD_REQUEST 0x80 // 4.00
#define COAP_CODE_NOT_FOUND 0x84   // 4.04

// Server Error (class 5)
#define COAP_CODE_INTERNAL_ERROR 0xA0 // 5.00

#endif // COAP_H