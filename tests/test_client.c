// tests/test_client.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "../src/coap.h"
#include <unistd.h>

typedef struct {
    const char *server_ip;
    uint16_t server_port;
    int requests;
    int thread_id;
    int success;
    int fail;
} worker_arg_t;

void *worker(void *arg) {
    worker_arg_t *a = (worker_arg_t*)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return NULL; }

    struct sockaddr_in srv;
    memset(&srv,0,sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(a->server_port);
    inet_pton(AF_INET, a->server_ip, &srv.sin_addr);

    for (int i=0;i<a->requests;i++) {
        coap_message_t msg;
        coap_init_message(&msg);
        msg.version = COAP_VERSION;
        msg.type = COAP_TYPE_CON;
        msg.tkl = 0;
        msg.code = COAP_CODE_GET;
        msg.message_id = (uint16_t)( (rand() & 0xFFFF) );

        uint8_t out[1024];
        int outlen = coap_serialize(&msg, out, sizeof(out));
        if (outlen <= 0) { a->fail++; continue; }

        ssize_t sent = sendto(sock, out, outlen, 0, (struct sockaddr*)&srv, sizeof(srv));
        if (sent < 0) { a->fail++; continue; }

        // wait for reply with timeout
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds); FD_SET(sock, &rfds);
        tv.tv_sec = 2; tv.tv_usec = 0; // 2s timeout

        int rv = select(sock+1, &rfds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(sock, &rfds)) {
            uint8_t in[2048];
            struct sockaddr_in from; socklen_t flen = sizeof(from);
            ssize_t r = recvfrom(sock, in, sizeof(in), 0, (struct sockaddr*)&from, &flen);
            if (r > 0) {
                coap_message_t resp;
                if (coap_parse(in, r, &resp) == COAP_OK) {
                    a->success++;
                    if (resp.payload) free(resp.payload);
                } else {
                    a->fail++;
                }
            } else a->fail++;
        } else {
            a->fail++;
        }
        // small delay to avoid flooding the server instantly
        usleep(10000);
    }
    close(sock);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,"Usage: %s <server-ip> <port> <threads> <requests-per-thread>\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    uint16_t port = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int reqs = atoi(argv[4]);

    pthread_t *tids = calloc(threads, sizeof(pthread_t));
    worker_arg_t *args = calloc(threads, sizeof(worker_arg_t));

    for (int i=0;i<threads;i++) {
        args[i].server_ip = ip;
        args[i].server_port = port;
        args[i].requests = reqs;
        args[i].thread_id = i;
        args[i].success = 0;
        args[i].fail = 0;
        pthread_create(&tids[i], NULL, worker, &args[i]);
    }

    int total_success=0, total_fail=0;
    for (int i=0;i<threads;i++) {
        pthread_join(tids[i], NULL);
        total_success += args[i].success;
        total_fail += args[i].fail;
        printf("Thread %d: success=%d fail=%d\n", i, args[i].success, args[i].fail);
    }

    printf("TOTAL: success=%d fail=%d\n", total_success, total_fail);

    free(tids); free(args);
    return 0;
}