// client.c - simple CoAP console client for testing the server
#include "coap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1500

static void usage(const char *prog) {
    printf("Uso: %s <server_ip> <method> [uri-path] [payload]\n", prog);
    printf("Ejemplos:\n");
    printf("  %s 127.0.0.1 GET\n", prog);
    printf("  %s 127.0.0.1 POST sensor \"temp=25.5,hum=40\"\n", prog);
    printf("  %s 127.0.0.1 PUT sensor/1 \"1=temp=26.0,hum=42\"\n", prog);
    printf("  %s 127.0.0.1 DELETE sensor/1 \"1\"\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    const char *method = argv[2];
    const char *uri_path = argc >= 4 ? argv[3] : NULL;
    const char *payload_str = argc >= 5 ? argv[4] : NULL;

    // Map method string to CoAP code
    uint8_t code;
    if (strcmp(method, "GET") == 0) code = COAP_CODE_GET;
    else if (strcmp(method, "POST") == 0) code = COAP_CODE_POST;
    else if (strcmp(method, "PUT") == 0) code = COAP_CODE_PUT;
    else if (strcmp(method, "DELETE") == 0) code = COAP_CODE_DELETE;
    else {
        fprintf(stderr, "Método no soportado: %s\n", method);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5683); // puerto fijo
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Construir mensaje CoAP
    coap_message_t req;
    coap_init_message(&req);
    req.type = COAP_TYPE_CON;
    req.code = code;
    req.message_id = rand() % 65535;
    req.tkl = 0;

    if (uri_path) {
        // dividir uri_path en segmentos
        char tmp[256];
        strncpy(tmp, uri_path, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';
        char *tok = strtok(tmp, "/");
        while (tok) {
            coap_add_option(&req, 11, (uint8_t*)tok, strlen(tok)); // Uri-Path
            tok = strtok(NULL, "/");
        }
    }

    if (payload_str) {
        req.payload = (uint8_t*)payload_str;
        req.payload_len = strlen(payload_str);
    }

    uint8_t buffer[BUF_SIZE];
    int len = coap_serialize(&req, buffer, sizeof(buffer));
    if (len <= 0) {
        fprintf(stderr, "Error serializando mensaje\n");
        coap_free_message(&req);
        return 1;
    }

    sendto(sock, buffer, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    printf("Enviado %s a %s\n", method, server_ip);

    // Recibir respuesta
    uint8_t rx[BUF_SIZE];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int rlen = recvfrom(sock, rx, sizeof(rx), 0,
                        (struct sockaddr*)&from, &fromlen);
    if (rlen > 0) {
        coap_message_t resp;
        if (coap_parse(rx, rlen, &resp) == COAP_OK) {
            printf("Respuesta: Code=%u, MID=%u\n", resp.code, resp.message_id);
            if (resp.payload_len > 0) {
                printf("Payload: %.*s\n", (int)resp.payload_len, resp.payload);
            }
            coap_free_message(&resp);
        } else {
            printf("Error parseando respuesta\n");
        }
    } else {
        perror("recvfrom");
    }

    coap_free_message(&req);
    close(sock);
    return 0;
}
