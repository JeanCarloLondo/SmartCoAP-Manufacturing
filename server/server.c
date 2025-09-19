// server.c
#include "coap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#define SERVER_PORT 5683
#define BUFFER_SIZE 1500

// ========== Simple request handler ==========
// This function inspects the parsed CoAP message and builds a response.
static void handle_coap_request(const coap_message_t *req,
                                struct sockaddr_in *client_addr,
                                int sockfd)
{
    coap_message_t resp;
    coap_init_message(&resp);
    resp.type = COAP_TYPE_ACK;
    resp.message_id = req->message_id;
    resp.code = 69; // 2.05 Content

    const char *payload_str = "Hello from SmartCoAP server!";
    resp.payload_len = strlen(payload_str);
    resp.payload = (uint8_t *)malloc(resp.payload_len);
    if (resp.payload)
        memcpy(resp.payload, payload_str, resp.payload_len);

    // Serialize response
    uint8_t out_buf[BUFFER_SIZE];
    int len = coap_serialize(&resp, out_buf, sizeof(out_buf));
    if (len > 0)
    {
        sendto(sockfd, (const char *)out_buf, len, 0,
               (struct sockaddr *)client_addr, sizeof(*client_addr));
    }

    coap_free_message(&resp);
}

// ========== Main server loop ==========
int main(int argc, char *argv[])
{
    int port = SERVER_PORT;
    FILE *logf = stdout; // log file (stdout by default)

    if (argc >= 2)
    {
        port = atoi(argv[1]);
    }
    if (argc >= 3)
    {
        logf = fopen(argv[2], "a");
        if (!logf)
        {
            perror("fopen log file");
            return EXIT_FAILURE;
        }
        dup2(fileno(logf), STDOUT_FILENO);
        dup2(fileno(logf), STDERR_FILENO);

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }
#endif

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind");
#if defined(_WIN32) || defined(_WIN64)
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return EXIT_FAILURE;
    }

    printf("CoAP server listening on port %d...\n", SERVER_PORT);

    uint8_t buffer[BUFFER_SIZE];
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);

    while (1)
    {
        ssize_t n = recvfrom(sockfd, (char *)buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&clientaddr, &clientlen);
        if (n < 0)
        {
            perror("recvfrom");
            continue;
        }

        coap_message_t req;
        int ret = coap_parse(buffer, (size_t)n, &req);
        if (ret != COAP_OK)
        {
            fprintf(stderr, "Failed to parse CoAP message (err=%d)\n", ret);
            continue;
        }

        printf("Received CoAP message: MID=%u Code=%u Type=%u Options=%zu PayloadLen=%zu\n",
               req.message_id, req.code, req.type, req.options_count, req.payload_len);

        handle_coap_request(&req, &clientaddr, sockfd);

        coap_free_message(&req);
    }

#if defined(_WIN32) || defined(_WIN64)
    closesocket(sockfd);
    WSACleanup();
#else
    close(sockfd);
#endif

    return EXIT_SUCCESS;
}