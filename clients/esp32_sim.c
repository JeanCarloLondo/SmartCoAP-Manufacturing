// clients/esp32_sim.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "../src/coap.h" // adjust path as needed

// Parameters
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 5683
#define MAX_BUF 1024

// Retransmission policy (simple: max 4 tries, initial wait 2s, exponential backoff)
#define MAX_RETRIES 4
#define INITIAL_WAIT_MS 2000

static uint16_t random_mid(void) {
    return (uint16_t)(rand() & 0xFFFF);
}

static int send_coap_and_wait_ack(int sock, struct sockaddr_in *srv, coap_message_t *msg, int timeout_ms) {
    uint8_t out[MAX_BUF];
    int outlen = coap_serialize(msg, out, sizeof(out));
    if (outlen <= 0) return -1;

    ssize_t s = sendto(sock, out, outlen, 0, (struct sockaddr*)srv, sizeof(*srv));
    if (s < 0) return -1;

    // wait for reply with timeout
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rv = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (rv > 0 && FD_ISSET(sock, &rfds)) {
        uint8_t in[MAX_BUF];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t r = recvfrom(sock, in, sizeof(in), 0, (struct sockaddr*)&from, &flen);
        if (r > 0) {
            coap_message_t resp;
            if (coap_parse(in, r, &resp) == COAP_OK) {
                // If ACK type and same MID -> success
                if (resp.type == COAP_TYPE_ACK && resp.message_id == msg->message_id) {
                    if (resp.payload) {
                        printf("[client] Received ACK (MID=%u) with payload: %.*s\n",
                               resp.message_id, (int)resp.payload_len, resp.payload);
                    } else {
                        printf("[client] Received empty ACK (MID=%u)\n", resp.message_id);
                    }
                    coap_free_message(&resp);
                    return 0;
                } else if (resp.type == COAP_TYPE_RST) {
                    printf("[client] Received RST for MID=%u\n", resp.message_id);
                    coap_free_message(&resp);
                    return -2; // server reset
                } else {
                    // other response types — treat as failure for this test
                    coap_free_message(&resp);
                    return -3;
                }
            } else {
                return -4; // parse error
            }
        } else return -5;
    } else {
        return 1; // timeout (no reply)
    }
}

int main(int argc, char **argv) {
    srand(time(NULL) ^ getpid());

    const char *server_ip = argc > 1 ? argv[1] : DEFAULT_SERVER_IP;
    int server_port = argc > 2 ? atoi(argv[2]) : DEFAULT_SERVER_PORT;
    int period_sec = argc > 3 ? atoi(argv[3]) : 5; // send each X seconds
    int runs = argc > 4 ? atoi(argv[4]) : 5;       // how many posts to send

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv;
    memset(&srv,0,sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &srv.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP\n");
        close(sock); return 1;
    }

    for (int i=0;i<runs;i++) {
        // Build CoAP POST with payload = temperature value (simulated)
        coap_message_t msg;
        coap_init_message(&msg);
        msg.version = COAP_VERSION;
        msg.type = COAP_TYPE_CON;
        msg.code = COAP_CODE_POST; // method POST
        msg.message_id = random_mid();

        // payload example: temp=25.3,hum=40.1
        char payload[64];
        float temp = 20.0f + (rand()%1000)/100.0f; // 20.00 .. 29.99
        float hum  = 30.0f + (rand()%700)/10.0f;   // 30.0 .. 99.9
        snprintf(payload, sizeof(payload), "temp=%.2f,hum=%.1f", temp, hum);
        msg.payload = (uint8_t*)payload;
        msg.payload_len = strlen(payload);

        printf("[client] Sending CON POST MID=%u payload=\"%s\"\n", msg.message_id, payload);

        int attempt = 0;
        int wait_ms = INITIAL_WAIT_MS;
        int rc = 1;
        while (attempt < MAX_RETRIES) {
            rc = send_coap_and_wait_ack(sock, &srv, &msg, wait_ms);
            if (rc == 0) {
                // success
                break;
            } else if (rc == 1) {
                // timeout -> retransmit
                attempt++;
                printf("[client] no ACK, retransmit attempt %d (wait %d ms)\n", attempt, wait_ms);
                wait_ms *= 2;
                continue;
            } else {
                // other error — break and report
                printf("[client] receive error code %d\n", rc);
                break;
            }
        }

        if (rc == 0) {
            printf("[client] POST acknowledged, stored by server (expected TC-003.1)\n");
        } else if (rc == 1) {
            printf("[client] gave up after %d attempts (no ACK)\n", attempt);
        }

        sleep(period_sec);
    }

    close(sock);
    return 0;
}