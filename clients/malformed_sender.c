// clients/malformed_sender.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 5683

int main(int argc, char **argv) {
    const char *server_ip = argc > 1 ? argv[1] : DEFAULT_SERVER_IP;
    int server_port = argc > 2 ? atoi(argv[2]) : DEFAULT_SERVER_PORT;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv;
    memset(&srv,0,sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &srv.sin_addr);

    // Build a malformed CoAP packet: wrong version (e.g., 3 instead of 1)
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = (3 << 6); // version=3 (invalid), type=0, tkl=0
    buf[1] = 0x01;     // code GET (but version invalid)
    buf[2] = 0x00; buf[3] = 0x01; // message id 1

    ssize_t s = sendto(sock, buf, 4, 0, (struct sockaddr*)&srv, sizeof(srv));
    if (s < 0) perror("sendto");
    else printf("Malformed packet sent (version=3) to %s:%d\n", server_ip, server_port);

    close(sock);
    return 0;
}