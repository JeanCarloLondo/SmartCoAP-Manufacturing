// server.c  (unificado: concurrencia + CRUD + Uri-Path handling + portable threads)
#include "coap.h"
#include "storage.h"
#include "db.h"
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
typedef HANDLE thread_t;
// Define STDOUT_FILENO and STDERR_FILENO for Windows
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
typedef pthread_t thread_t;
#endif

#define DEFAULT_PORT 5683
#define BUF_SIZE 1500

typedef struct
{
    int sock;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    uint8_t buffer[BUF_SIZE];
    ssize_t msg_len;
    FILE *log_file;
} client_task_t;

/* Helpers */
static void init_response_from_request(const coap_message_t *req, coap_message_t *resp)
{
    coap_init_message(resp);
    resp->version = COAP_VERSION;
    resp->message_id = req->message_id;
    resp->tkl = req->tkl;
    if (resp->tkl && req->tkl <= COAP_MAX_TOKEN_LEN)
        memcpy(resp->token, req->token, req->tkl);

    if (req->type == COAP_TYPE_CON)
        resp->type = COAP_TYPE_ACK;
    else if (req->type == COAP_TYPE_NON)
        resp->type = COAP_TYPE_NON;
    else
        resp->type = COAP_TYPE_RST;
}

static char *extract_uri_path(const coap_message_t *req)
{
    if (!req || req->options_count == 0)
        return NULL;
    size_t total = 0;
    for (size_t i = 0; i < req->options_count; ++i)
    {
        if (req->options[i].number == 11)
        {
            total += req->options[i].length + 1;
        }
    }
    if (total == 0)
        return NULL;
    char *path = malloc(total + 1);
    if (!path)
        return NULL;
    path[0] = '\0';
    int first = 1;
    for (size_t i = 0; i < req->options_count; ++i)
    {
        if (req->options[i].number == 11)
        {
            if (!first)
                strcat(path, "/");
            else
                first = 0;
            strncat(path, (char *)req->options[i].value, req->options[i].length);
        }
    }
    return path;
}

static int is_numeric(const char *s)
{
    if (!s || *s == '\0')
        return 0;
    if (*s == '-')
        s++;
    while (*s)
    {
        if (*s < '0' || *s > '9')
            return 0;
        s++;
    }
    return 1;
}

/* Worker */
#if defined(_WIN32) || defined(_WIN64)
DWORD WINAPI handle_client(LPVOID arg)
#else
void *handle_client(void *arg)
#endif
{
    client_task_t *task = (client_task_t *)arg;

    coap_message_t req;
    memset(&req, 0, sizeof(req));

    if (coap_parse(task->buffer, (size_t)task->msg_len, &req) != COAP_OK)
    {
        fprintf(task->log_file, "Failed to parse CoAP message\n");
        free(task);
#if defined(_WIN32) || defined(_WIN64)
        return 0;
#else
        return NULL;
#endif
    }

    coap_message_t resp;
    init_response_from_request(&req, &resp);

    char *uri_path = extract_uri_path(&req);
    char tmpbuf[1024];

    switch (req.code)
    {
    case COAP_CODE_GET:
    {
        if (uri_path && is_numeric(uri_path))
        {
            int id = atoi(uri_path);
            char *val = storage_get(id);
            if (val)
            {
                resp.code = COAP_CODE_CONTENT;
                resp.payload = (uint8_t *)val;
                resp.payload_len = strlen(val);
            }
            else
                resp.code = COAP_CODE_NOT_FOUND;
        }
        else
        {
            char *all = storage_get(0);
            if (all)
            {
                resp.code = COAP_CODE_CONTENT;
                resp.payload = (uint8_t *)all;
                resp.payload_len = strlen(all);
            }
            else
                resp.code = COAP_CODE_NOT_FOUND;
        }
        break;
    }
    case COAP_CODE_POST:
    {
        if (req.payload && req.payload_len > 0)
        {
            snprintf(tmpbuf, sizeof(tmpbuf), "%.*s",
                     (int)req.payload_len, (char *)req.payload);
            int id = storage_add(0, tmpbuf);
            if (id >= 0)
            {
                snprintf(tmpbuf, sizeof(tmpbuf), "{\"id\":%d}", id);
                resp.code = COAP_CODE_CREATED;
                resp.payload_len = strlen(tmpbuf);
                resp.payload = (uint8_t *)malloc(resp.payload_len);
                memcpy(resp.payload, tmpbuf, resp.payload_len);
            }
            else
                resp.code = COAP_CODE_INTERNAL_ERROR;
        }
        else
            resp.code = COAP_CODE_BAD_REQUEST;
        break;
    }
    case COAP_CODE_PUT:
    {
        int id = -1;
        char value[512] = {0};
        if (req.payload && req.payload_len > 0)
        {
            char *p = strndup((char *)req.payload, req.payload_len);
            if (p)
            {
                char *eq = strchr(p, '=');
                if (eq)
                {
                    *eq = '\0';
                    if (is_numeric(p))
                    {
                        id = atoi(p);
                        strncpy(value, eq + 1, sizeof(value) - 1);
                    }
                }
                free(p);
            }
        }
        if (id >= 0 && value[0])
        {
            if (storage_update(id, value) == 0)
            {
                snprintf(tmpbuf, sizeof(tmpbuf), "{\"updated\":%d}", id);
                resp.code = COAP_CODE_CHANGED;
                resp.payload_len = strlen(tmpbuf);
                resp.payload = (uint8_t *)malloc(resp.payload_len);
                memcpy(resp.payload, tmpbuf, resp.payload_len);
            }
            else
                resp.code = COAP_CODE_NOT_FOUND;
        }
        else
            resp.code = COAP_CODE_BAD_REQUEST;
        break;
    }
    case COAP_CODE_DELETE:
    {
        int id = -1;
        if (req.payload && req.payload_len > 0)
        {
            char *p = strndup((char *)req.payload, req.payload_len);
            if (p && is_numeric(p))
                id = atoi(p);
            free(p);
        }
        if (id >= 0 && storage_delete(id) == 0)
        {
            snprintf(tmpbuf, sizeof(tmpbuf), "{\"deleted\":%d}", id);
            resp.code = COAP_CODE_DELETED;
            resp.payload_len = strlen(tmpbuf);
            resp.payload = (uint8_t *)malloc(resp.payload_len);
            memcpy(resp.payload, tmpbuf, resp.payload_len);
        }
        else
            resp.code = COAP_CODE_NOT_FOUND;
        break;
    }
    default:
        resp.code = COAP_CODE_BAD_REQUEST;
        break;
    }

    uint8_t out[BUF_SIZE];
    int len = coap_serialize(&resp, out, sizeof(out));
    if (len > 0)
        sendto(task->sock, (const char *)out, len, 0,
               (struct sockaddr *)&task->client_addr, task->addr_len);

    fprintf(task->log_file, "Processed MID=%u Code=%u Uri=%s\n",
            req.message_id, req.code, uri_path ? uri_path : "(none)");
    fflush(task->log_file);

    if (uri_path)
        free(uri_path);
    coap_free_message(&req);
    coap_free_message(&resp);
    free(task);

#if defined(_WIN32) || defined(_WIN64)
    return 0;
#else
    return NULL;
#endif
}

int main(int argc, char *argv[])
{

    char db_path[512];
snprintf(db_path, sizeof(db_path), "./coap_data.db");

char db_dir[512];
snprintf(db_dir, sizeof(db_dir), ".");
mkdir(db_dir, 0755);

if (db_init(db_path) != 0) {
    fprintf(stderr, "Error inicializando base de datos: %s\n", db_path);
    return EXIT_FAILURE;
}

    int port = DEFAULT_PORT;
    FILE *logf = stdout;

    if (argc >= 2)
        port = atoi(argv[1]);
    if (argc >= 3)
    {
        FILE *f = fopen(argv[2], "a");
        if (f)
        {
            fflush(stdout);
            fflush(stderr);
            freopen(argv[2], "a", stdout);
            freopen(argv[2], "a", stderr);
            setvbuf(stdout, NULL, _IOLBF, 0); // line buffered
            setvbuf(stderr, NULL, _IOLBF, 0);
            logf = f;
        }
        else
        {
            perror("fopen log");
        }
    }

#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }
#endif

    storage_init();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return EXIT_FAILURE;
    }

    printf("CoAP server listening on %d...\n", port);
    // inicializar storage
    if (storage_init() != 0)
    {
        fprintf(stderr, "Error inicializando storage\n");
        return EXIT_FAILURE;
    }
    printf("Storage initialized.\n");

    while (1)
    {
        client_task_t *task = malloc(sizeof(*task));
        if (!task)
            continue;
        task->sock = sock;
        task->addr_len = sizeof(task->client_addr);
        task->msg_len = recvfrom(sock, task->buffer, BUF_SIZE, 0,
                                 (struct sockaddr *)&task->client_addr, &task->addr_len);
        task->log_file = logf;
        if (task->msg_len <= 0)
        {
            free(task);
            continue;
        }

#if defined(_WIN32) || defined(_WIN64)
        thread_t tid = CreateThread(NULL, 0, handle_client, task, 0, NULL);
        if (tid == NULL)
        {
            fprintf(logf, "CreateThread failed\n");
            free(task);
            continue;
        }
        CloseHandle(tid);
#else
        thread_t tid;
        if (pthread_create(&tid, NULL, handle_client, task) != 0)
        {
            fprintf(logf, "pthread_create failed\n");
            free(task);
            continue;
        }
        pthread_detach(tid);
#endif
    }

#if defined(_WIN32) || defined(_WIN64)
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    storage_cleanup();
    db_close();
    return EXIT_SUCCESS;
}