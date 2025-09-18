#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_PORT 5683
#define SERVER_ADDR "127.0.0.1"

enum CoAPMethod { GET=1, POST=2, PUT=3, DELETE=4 };

void send_coap_request(int method, const char* payload) {
    int sockfd;
    struct sockaddr_in servaddr;

    unsigned char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    // Version=1, Type=CON, Token len=0
    buffer[0] = 0x40;
    buffer[1] = method;  // Code
    buffer[2] = 0x12;    // Message ID high
    buffer[3] = 0x34;    // Message ID low

    int len = 4;

    if (payload) {
        buffer[len++] = 0xFF; // Payload marker
        strcpy((char*)&buffer[len], payload);
        len += strlen(payload);
    }

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_ADDR, &servaddr.sin_addr);

    sendto(sockfd, buffer, len, 0, (struct sockaddr*)&servaddr, sizeof(servaddr));

    // Receive response
    socklen_t addrlen = sizeof(servaddr);
    int n = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0, (struct sockaddr*)&servaddr, &addrlen);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Response: %s\n", buffer+4+1); // skip header + 0xFF
    }

    close(sockfd);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: %s METHOD [PAYLOAD]\n", argv[0]);
        return 1;
    }

    char* method = argv[1];
    if (strcmp(method, "POST") == 0) send_coap_request(POST, argv[2]);
    else if (strcmp(method, "GET") == 0) send_coap_request(GET, NULL);
    else if (strcmp(method, "PUT") == 0) send_coap_request(PUT, argv[2]);
    else if (strcmp(method, "DELETE") == 0) send_coap_request(DELETE, NULL);
    else printf("Method not supported.\n");

    return 0;
}