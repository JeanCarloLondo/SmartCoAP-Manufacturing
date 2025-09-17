#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "coap.h"
#include "storage.h"

#define BUF_SIZE 1024

typedef struct
{
    int sock;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    uint8_t buffer[BUF_SIZE];
    size_t msg_len;
    FILE *log_file;
} client_task_t;

void *handle_client(void *arg)
{
    client_task_t *task = (client_task_t *)arg;

    coap_message_t req, resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    if (coap_parse(task->buffer, task->msg_len, &req) != COAP_OK)
    {
        // Build RST (reset)
        resp.version = COAP_VERSION;
        resp.type = COAP_TYPE_RST;
        resp.code = 0;
        resp.message_id = req.message_id;
        resp.tkl = 0;
        resp.payload = NULL;
        resp.payload_len = 0;

        uint8_t out_buf[BUF_SIZE];
        int len = coap_serialize(&resp, out_buf, sizeof(out_buf));
        sendto(task->sock, out_buf, len, 0,
               (struct sockaddr *)&task->client_addr, task->addr_len);
        fprintf(task->log_file, "RST sent\n");
        fflush(task->log_file);
        free(task);
        return NULL;
    }

    // Build base response
    resp.version = COAP_VERSION;
    resp.message_id = req.message_id;
    resp.tkl = req.tkl;
    memcpy(resp.token, req.token, req.tkl);

    if (req.type == COAP_TYPE_CON)
        resp.type = COAP_TYPE_ACK;
    else if (req.type == COAP_TYPE_NON)
        resp.type = COAP_TYPE_NON;
    else
        resp.type = COAP_TYPE_RST;

    int key = 1;

    switch (req.code)
    {
    case COAP_CODE_GET:
    {
        char *value = storage_get(key);
        if (value != NULL)
        {
            resp.code = COAP_CODE_CONTENT;
            resp.payload = (uint8_t *)value;
            resp.payload_len = strlen(value);
        }
        else
        {
            resp.code = COAP_CODE_NOT_FOUND;
        }

        uint8_t out_buf[BUF_SIZE];
        int len = coap_serialize(&resp, out_buf, sizeof(out_buf));
        sendto(task->sock, out_buf, len, 0,
               (struct sockaddr *)&task->client_addr, task->addr_len);

        if (value)
            free(value);
        break;
    }
    case COAP_CODE_POST:
    {
        storage_add(key, "new_value");
        resp.code = COAP_CODE_CREATED;
        break;
    }
    case COAP_CODE_PUT:
    {
        storage_update(key, "updated_value");
        resp.code = COAP_CODE_CHANGED;
        break;
    }
    case COAP_CODE_DELETE:
    {
        storage_delete(key);
        resp.code = COAP_CODE_DELETED;
        break;
    }
    default:
        resp.code = COAP_CODE_BAD_REQUEST;
    }

    uint8_t out_buf[BUF_SIZE];
    int len = coap_serialize(&resp, out_buf, sizeof(out_buf));
    sendto(task->sock, out_buf, len, 0,
           (struct sockaddr *)&task->client_addr, task->addr_len);

    fprintf(task->log_file, "Processed request MID=%d, Code=%d\n",
            req.message_id, req.code);
    fflush(task->log_file);

    if (req.payload)
        free(req.payload);
    free(task);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <PORT> <LogFile>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    FILE *log_file = fopen(argv[2], "a");
    if (!log_file)
    {
        perror("Log file");
        exit(1);
    }

    storage_init();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    fprintf(log_file, "Server listening on port %d\n", port);
    fflush(log_file);

    while (1)
    {
        client_task_t *task = malloc(sizeof(client_task_t));
        task->sock = sock;
        task->addr_len = sizeof(task->client_addr);
        task->msg_len = recvfrom(sock, task->buffer, BUF_SIZE, 0,
                                 (struct sockaddr *)&task->client_addr,
                                 &task->addr_len);
        task->log_file = log_file;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, task);
        pthread_detach(tid);
    }

    close(sock);
    fclose(log_file);
    storage_cleanup();
    return 0;
}