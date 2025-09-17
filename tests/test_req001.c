#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/coap.h"
#include <arpa/inet.h>

/*
 * Simulated server handler:
 * - Parse the incoming bytes
 * - If version mismatch -> build RST and serialize
 * - If type == CON -> build empty ACK and serialize
 * - If type == NON -> process, but return no bytes (NULL)
 * - Else -> build RST
 *
 * Returns: positive length of response in out_resp_len and pointer to buffer (malloced) if a response is produced.
 * If no response: returns 0 and *out_resp = NULL.
 * On parse error: returns negative.
 */
int simulated_server_handle(const uint8_t *in_buf, size_t in_len, uint8_t **out_resp, size_t *out_resp_len) {
    coap_message_t msg;
    coap_init_message(&msg);
    int st = coap_parse(in_buf, in_len, &msg);
    if (st == COAP_ERR_VERSION_MISMATCH) {
        // Build RST using parsed msg.message_id
        coap_message_t rst;
        coap_build_rst_for(&msg, &rst);
        size_t max_out = 256;
        uint8_t *buf = (uint8_t*) malloc(max_out);
        int n = coap_serialize(&rst, buf, max_out);
        if (n < 0) { free(buf); return n; }
        *out_resp = buf;
        *out_resp_len = (size_t)n;
        return (int)n;
    } else if (st != COAP_OK) {
        // For parsing errors (options, truncation) we choose to send RST with msg.message_id if available,
        // else no response. Simpler: return negative to indicate failure.
        return st;
    }

    if (msg.type == COAP_TYPE_CON) {
        coap_message_t ack;
        coap_build_empty_ack(&msg, &ack);
        size_t max_out = 256;
        uint8_t *buf = (uint8_t*) malloc(max_out);
        int n = coap_serialize(&ack, buf, max_out);
        if (n < 0) { free(buf); coap_free_message(&msg); return n; }
        *out_resp = buf;
        *out_resp_len = (size_t)n;
        coap_free_message(&msg);
        return (int)n;
    } else if (msg.type == COAP_TYPE_NON) {
        // No response required
        *out_resp = NULL;
        *out_resp_len = 0;
        coap_free_message(&msg);
        return 0;
    } else {
        // Unexpected type -> RST
        coap_message_t rst;
        coap_build_rst_for(&msg, &rst);
        size_t max_out = 256;
        uint8_t *buf = (uint8_t*) malloc(max_out);
        int n = coap_serialize(&rst, buf, max_out);
        if (n < 0) { free(buf); coap_free_message(&msg); return n; }
        *out_resp = buf;
        *out_resp_len = (size_t)n;
        coap_free_message(&msg);
        return (int)n;
    }
}

/* Helpers to build test messages (serialize using coap_serialize) */
int build_con_msg(uint16_t mid, const uint8_t *token, uint8_t tkl, const uint8_t *payload, size_t payload_len, uint8_t *out_buf, size_t out_buf_len) {
    coap_message_t m;
    coap_init_message(&m);
    m.type = COAP_TYPE_CON;
    m.code = COAP_METHOD_POST;
    m.message_id = mid;
    m.tkl = tkl > COAP_MAX_TOKEN_LEN ? 0 : tkl;
    if (m.tkl && token) memcpy(m.token, token, m.tkl);
    if (payload_len) {
        m.payload = (uint8_t*) malloc(payload_len);
        memcpy(m.payload, payload, payload_len);
        m.payload_len = payload_len;
    }
    int n = coap_serialize(&m, out_buf, out_buf_len);
    if (m.payload) { free(m.payload); m.payload = NULL; }
    return n;
}

int build_non_msg(uint16_t mid, const uint8_t *token, uint8_t tkl, const uint8_t *payload, size_t payload_len, uint8_t *out_buf, size_t out_buf_len) {
    coap_message_t m;
    coap_init_message(&m);
    m.type = COAP_TYPE_NON;
    m.code = COAP_METHOD_POST;
    m.message_id = mid;
    m.tkl = tkl > COAP_MAX_TOKEN_LEN ? 0 : tkl;
    if (m.tkl && token) memcpy(m.token, token, m.tkl);
    if (payload_len) {
        m.payload = (uint8_t*) malloc(payload_len);
        memcpy(m.payload, payload, payload_len);
        m.payload_len = payload_len;
    }
    int n = coap_serialize(&m, out_buf, out_buf_len);
    if (m.payload) { free(m.payload); m.payload = NULL; }
    return n;
}

int build_invalid_version_msg(uint16_t mid, uint8_t *out_buf, size_t out_buf_len) {
    // Build a minimal header with version = 2 (invalid), type=CON, tkl=0, code=GET
    if (out_buf_len < 4) return COAP_ERR_TRUNCATED;
    uint8_t first = ((2 & 0x03) << 6) | ((COAP_TYPE_CON & 0x03) << 4) | 0;
    out_buf[0] = first;
    out_buf[1] = COAP_METHOD_GET;
    uint16_t mid_net = htons(mid);
    out_buf[2] = (uint8_t)((mid_net >> 8) & 0xFF);
    out_buf[3] = (uint8_t)(mid_net & 0xFF);
    return 4;
}

int main(void) {
    printf("=== Running REQ-001 (CoAP) tests ===\n");

    // TC-001.1 CON -> ACK
    {
        uint8_t buf[256];
        int n = build_con_msg(0x1234, (const uint8_t*)"\xAA\xBB", 2, (const uint8_t*)"hello", 5, buf, sizeof(buf));
        if (n < 0) { printf("Failed to build CON message\n"); return 1; }
        uint8_t *resp = NULL;
        size_t resp_len = 0;
        int r = simulated_server_handle(buf, (size_t)n, &resp, &resp_len);
        if (r <= 0 || resp == NULL) { printf("TC-001.1 FAILED: expected ACK\n"); return 1; }
        // parse response
        coap_message_t parsed;
        coap_init_message(&parsed);
        int pst = coap_parse(resp, resp_len, &parsed);
        if (pst != COAP_OK) { printf("TC-001.1 FAILED: cannot parse response\n"); free(resp); return 1; }
        if (parsed.type != COAP_TYPE_ACK) { printf("TC-001.1 FAILED: response not ACK\n"); coap_free_message(&parsed); free(resp); return 1; }
        if (parsed.message_id != 0x1234) { printf("TC-001.1 FAILED: MID mismatch\n"); coap_free_message(&parsed); free(resp); return 1; }
        printf("TC-001.1 PASS: CON -> ACK\n");
        coap_free_message(&parsed);
        free(resp);
    }

    // TC-001.2 NON -> no ACK
    {
        uint8_t buf[256];
        int n = build_non_msg(0x2222, (const uint8_t*)"\x01", 1, (const uint8_t*)"data", 4, buf, sizeof(buf));
        if (n < 0) { printf("Failed to build NON message\n"); return 1; }
        uint8_t *resp = NULL;
        size_t resp_len = 0;
        int r = simulated_server_handle(buf, (size_t)n, &resp, &resp_len);
        if (r != 0 || resp != NULL) { printf("TC-001.2 FAILED: expected no response\n"); if (resp) free(resp); return 1; }
        printf("TC-001.2 PASS: NON processed, no ACK\n");
    }

    // TC-001.3 invalid version -> RST
    {
        uint8_t buf[256];
        int n = build_invalid_version_msg(0x5555, buf, sizeof(buf));
        if (n < 0) { printf("Failed to build invalid version message\n"); return 1; }
        uint8_t *resp = NULL;
        size_t resp_len = 0;
        int r = simulated_server_handle(buf, (size_t)n, &resp, &resp_len);
        if (r <= 0 || resp == NULL) { printf("TC-001.3 FAILED: expected RST\n"); return 1; }
        coap_message_t parsed;
        coap_init_message(&parsed);
        int pst = coap_parse(resp, resp_len, &parsed);
        if (pst != COAP_OK) { printf("TC-001.3 FAILED: cannot parse response\n"); free(resp); return 1; }
        if (parsed.type != COAP_TYPE_RST) { printf("TC-001.3 FAILED: response not RST\n"); coap_free_message(&parsed); free(resp); return 1; }
        if (parsed.message_id != 0x5555) { printf("TC-001.3 FAILED: MID mismatch\n"); coap_free_message(&parsed); free(resp); return 1; }
        printf("TC-001.3 PASS: invalid version -> RST\n");
        coap_free_message(&parsed);
        free(resp);
    }

    printf("=== All REQ-001 tests PASSED ===\n");
    return 0;
}