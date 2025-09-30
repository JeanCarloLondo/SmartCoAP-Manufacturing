#include "coap.h"

#include "../src/db.h"
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
#include <ctype.h>
#include <strings.h>

typedef pthread_t thread_t;
#endif

#define DEFAULT_PORT 5683
#define BUF_SIZE 8192
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

/* trim in-place, returns pointer to trimmed string (same buffer) */
static char *trim_inplace(char *s)
{
    if (!s)
        return s;
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }
    return s;
}

/* extract numeric token after token (like "temp" or "hum") into out (null-terminated).
   Returns 1 if something copied, 0 otherwise. Accepts colon or space separators and decimals.
*/
static int extract_number_after(const char *s, const char *token, char *out, size_t outsz)
{
    if (!s || !token || !out)
        return 0;
    const char *p = strstr(s, token);
    if (!p)
        return 0;
    p = strchr(p, ':');
    if (!p)
        p = strchr(p, ' ');
    if (!p)
        return 0;
    p++;
    while (*p && isspace((unsigned char)*p))
        p++;
    size_t i = 0;
    if (*p == '+' || *p == '-')
    {
        if (i + 1 < outsz)
            out[i++] = *p++;
    }
    while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == ','))
    {
        if (i + 1 >= outsz)
            break;
        if (*p == ',')
            out[i++] = '.';
        else
            out[i++] = *p;
        p++;
    }
    if (i == 0)
    {
        out[0] = '\0';
        return 0;
    }
    out[i] = '\0';
    return 1;
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

/* Helper to check if uri_path is sensor/<num> and return sensor number, else -1 */
static int parse_sensor_uri(const char *uri_path)
{
    if (!uri_path)
        return -1;
    const char *p = uri_path;
    // accept "sensor" or "sensor/<n>"
    if (strncmp(p, "sensor", 6) != 0)
        return -1;
    p += 6;
    if (*p == '\0')
        return -1;
    if (*p == '/')
        p++;
    if (!is_numeric(p))
        return -1;
    return atoi(p);
}

/* Worker function - SQLite */
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
        // If uri_path is numeric (id) OR sensor/<id> -> treat as id-get
        int id = -1;
        if (uri_path)
        {
            if (is_numeric(uri_path))
            {
                id = atoi(uri_path);
            }
            else
            {
                int sensor_as_id = parse_sensor_uri(uri_path);
                if (sensor_as_id > 0)
                {
                    id = sensor_as_id;
                }
            }
        }

        if (id > 0)
        {
            char *val = db_get_by_id(id); // specific ID
            if (val)
            {
                resp.code = COAP_CODE_CONTENT;
                resp.payload = (uint8_t *)val;
                resp.payload_len = strlen(val);
                fprintf(task->log_file, "GET id=%d: Found\n", id);
            }
            else
            {
                resp.code = COAP_CODE_NOT_FOUND;
                fprintf(task->log_file, "GET id=%d: Not found\n", id);
            }
        }
        else
        {
            // For now, GET all (could later filter by sensor)
            char *all = db_get_all();
            if (all)
            {
                resp.code = COAP_CODE_CONTENT;
                resp.payload = (uint8_t *)all;
                resp.payload_len = strlen(all);
                fprintf(task->log_file, "GET all: Success\n");
            }
            else
            {
                resp.code = COAP_CODE_INTERNAL_ERROR;
                fprintf(task->log_file, "GET all: Database error\n");
            }
        }
        break;
    }
    case COAP_CODE_POST:
    {
        if (req.payload && req.payload_len > 0)
        {
            snprintf(tmpbuf, sizeof(tmpbuf), "%.*s",
                     (int)req.payload_len, (char *)req.payload);

            // If URI is sensor/<n> then insert with sensor id
            int sensor_id = -1;
            if (uri_path)
            {
                // support both "sensor" (no numeric) and "sensor/<n>"
                sensor_id = parse_sensor_uri(uri_path);
            }

            // explicit id check (payload starts with "N " or "N=") preserved:
            int explicit_id = -1;
            char *payload_copy = strndup(tmpbuf, sizeof(tmpbuf));
            if (payload_copy)
            {
                char *sep = strpbrk(payload_copy, " =");
                if (sep)
                {
                    *sep = '\0';
                    if (is_numeric(payload_copy))
                        explicit_id = atoi(payload_copy);
                }
                free(payload_copy);
            }

            int id = -1;
            if (explicit_id > 0)
            {
                char *value_part = strpbrk(tmpbuf, " =");
                if (value_part)
                {
                    value_part++;
                    id = db_insert_with_id(explicit_id, value_part);
                    if (id > 0)
                    {
                        snprintf(tmpbuf, sizeof(tmpbuf), "{\"id\":%d}", id);
                        resp.code = COAP_CODE_CREATED;
                        resp.payload_len = strlen(tmpbuf);
                        resp.payload = (uint8_t *)malloc(resp.payload_len);
                        if (resp.payload)
                            memcpy(resp.payload, tmpbuf, resp.payload_len);
                        fprintf(task->log_file, "POST: Created id=%d (explicit)\n", id);
                    }
                    else
                    {
                        resp.code = COAP_CODE_BAD_REQUEST;
                        fprintf(task->log_file, "POST: explicit id=%d insert failed\n", explicit_id);
                    }
                }
                else
                {
                    resp.code = COAP_CODE_BAD_REQUEST;
                    fprintf(task->log_file, "POST: explicit id provided but no value\n");
                }
            }
            else if (sensor_id > 0)
            {
                // insert tying to sensor id (db_insert_with_sensor must exist)
                id = db_insert_with_sensor(sensor_id, tmpbuf);
                if (id > 0)
                {
                    snprintf(tmpbuf, sizeof(tmpbuf), "{\"id\":%d}", id);
                    resp.code = COAP_CODE_CREATED;
                    resp.payload_len = strlen(tmpbuf);
                    resp.payload = (uint8_t *)malloc(resp.payload_len);
                    if (resp.payload)
                        memcpy(resp.payload, tmpbuf, resp.payload_len);
                    fprintf(task->log_file, "POST: Created id=%d (sensor=%d)\n", id, sensor_id);
                }
                else
                {
                    resp.code = COAP_CODE_INTERNAL_ERROR;
                    fprintf(task->log_file, "POST: sensor insert failed (sensor=%d)\n", sensor_id);
                }
            }
            else
            {
                // normal autoincrement insert
                id = db_insert(tmpbuf);
                if (id > 0)
                {
                    snprintf(tmpbuf, sizeof(tmpbuf), "{\"id\":%d}", id);
                    resp.code = COAP_CODE_CREATED;
                    resp.payload_len = strlen(tmpbuf);
                    resp.payload = (uint8_t *)malloc(resp.payload_len);
                    if (resp.payload)
                        memcpy(resp.payload, tmpbuf, resp.payload_len);
                    fprintf(task->log_file, "POST: Created id=%d\n", id);
                }
                else
                {
                    resp.code = COAP_CODE_INTERNAL_ERROR;
                    fprintf(task->log_file, "POST: Database insert failed\n");
                }
            }
        }
        else
        {
            resp.code = COAP_CODE_BAD_REQUEST;
            fprintf(task->log_file, "POST: Empty payload\n");
        }
        break;
    }

    case COAP_CODE_PUT:
    {
        int id = -1;
        char *payload = NULL;
        if (req.payload && req.payload_len > 0)
        {
            char *p = strndup((char *)req.payload, req.payload_len);
            if (p)
            {
                /* Accept formats: "id=value" or "id = value" (tolerant to spaces) */
                char *eq = strchr(p, '=');
                if (eq)
                {
                    *eq = '\0';
                    trim_inplace(p);
                    trim_inplace(eq + 1);
                    if (is_numeric(p))
                    {
                        id = atoi(p);
                        payload = strdup(eq + 1);
                    }
                }
                free(p);
            }
        }

        if (id > 0 && payload && payload[0])
        {
            /* Detect intent: update only temp, only hum, both, or full replace */
            int has_temp = (strstr(payload, "temp") != NULL);
            int has_hum = (strstr(payload, "hum") != NULL);

            if (has_temp && has_hum)
            {
                /* extract both numbers and create normalized JSON {"temp":X,"hum":Y} */
                char tbuf[64] = {0}, hbuf[64] = {0};
                int okt = extract_number_after(payload, "temp", tbuf, sizeof(tbuf));
                int okh = extract_number_after(payload, "hum", hbuf, sizeof(hbuf));
                if (!okt)
                    strcpy(tbuf, "0");
                if (!okh)
                    strcpy(hbuf, "0");
                char combined[128];
                snprintf(combined, sizeof(combined), "{\"temp\":%s,\"hum\":%s}", tbuf, hbuf);
                if (db_update(id, combined) == 0)
                {
                    snprintf(tmpbuf, sizeof(tmpbuf), "{\"updated\":%d}", id);
                    resp.code = COAP_CODE_CHANGED;
                    resp.payload_len = strlen(tmpbuf);
                    resp.payload = (uint8_t *)malloc(resp.payload_len);
                    if (resp.payload)
                        memcpy(resp.payload, tmpbuf, resp.payload_len);
                    fprintf(task->log_file, "PUT: Updated id=%d (temp+hum)\n", id);
                }
                else
                {
                    resp.code = COAP_CODE_NOT_FOUND;
                    fprintf(task->log_file, "PUT: id=%d not found (temp+hum)\n", id);
                }
            }
            else if (has_temp)
            {
                char tbuf[64] = {0};
                if (extract_number_after(payload, "temp", tbuf, sizeof(tbuf)))
                {
                    if (db_update_field_in_json(id, "temp", tbuf) == 0)
                    {
                        snprintf(tmpbuf, sizeof(tmpbuf), "{\"updated\":%d}", id);
                        resp.code = COAP_CODE_CHANGED;
                        resp.payload_len = strlen(tmpbuf);
                        resp.payload = (uint8_t *)malloc(resp.payload_len);
                        if (resp.payload)
                            memcpy(resp.payload, tmpbuf, resp.payload_len);
                        fprintf(task->log_file, "PUT: Updated temp id=%d\n", id);
                    }
                    else
                    {
                        resp.code = COAP_CODE_NOT_FOUND;
                        fprintf(task->log_file, "PUT: id=%d not found (temp)\n", id);
                    }
                }
                else
                {
                    resp.code = COAP_CODE_BAD_REQUEST;
                    fprintf(task->log_file, "PUT: temp value parse error for id=%d\n", id);
                }
            }
            else if (has_hum)
            {
                char hbuf[64] = {0};
                if (extract_number_after(payload, "hum", hbuf, sizeof(hbuf)))
                {
                    if (db_update_field_in_json(id, "hum", hbuf) == 0)
                    {
                        snprintf(tmpbuf, sizeof(tmpbuf), "{\"updated\":%d}", id);
                        resp.code = COAP_CODE_CHANGED;
                        resp.payload_len = strlen(tmpbuf);
                        resp.payload = (uint8_t *)malloc(resp.payload_len);
                        if (resp.payload)
                            memcpy(resp.payload, tmpbuf, resp.payload_len);
                        fprintf(task->log_file, "PUT: Updated hum id=%d\n", id);
                    }
                    else
                    {
                        resp.code = COAP_CODE_NOT_FOUND;
                        fprintf(task->log_file, "PUT: id=%d not found (hum)\n", id);
                    }
                }
                else
                {
                    resp.code = COAP_CODE_BAD_REQUEST;
                    fprintf(task->log_file, "PUT: hum value parse error for id=%d\n", id);
                }
            }
            else
            {
                /* Full replace payload */
                if (db_update(id, payload) == 0)
                {
                    snprintf(tmpbuf, sizeof(tmpbuf), "{\"updated\":%d}", id);
                    resp.code = COAP_CODE_CHANGED;
                    resp.payload_len = strlen(tmpbuf);
                    resp.payload = (uint8_t *)malloc(resp.payload_len);
                    if (resp.payload)
                        memcpy(resp.payload, tmpbuf, resp.payload_len);
                    fprintf(task->log_file, "PUT: Updated id=%d (full replace)\n", id);
                }
                else
                {
                    resp.code = COAP_CODE_NOT_FOUND;
                    fprintf(task->log_file, "PUT: id=%d not found (full)\n", id);
                }
            }

            free(payload);
        }
        else
        {
            resp.code = COAP_CODE_BAD_REQUEST;
            fprintf(task->log_file, "PUT: Invalid format (expected: id=value)\n");
        }
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
        if (id > 0 && db_delete(id) == 0)
        {
            snprintf(tmpbuf, sizeof(tmpbuf), "{\"deleted\":%d}", id);
            resp.code = COAP_CODE_DELETED;
            resp.payload_len = strlen(tmpbuf);
            resp.payload = (uint8_t *)malloc(resp.payload_len);
            if (resp.payload)
            {
                memcpy(resp.payload, tmpbuf, resp.payload_len);
            }
            fprintf(task->log_file, "DELETE: Deleted id=%d\n", id);
        }
        else
        {
            resp.code = COAP_CODE_NOT_FOUND;
            fprintf(task->log_file, "DELETE: id=%d not found or invalid\n", id);
        }
        break;
    }
    default:
        resp.code = COAP_CODE_BAD_REQUEST;
        fprintf(task->log_file, "Unsupported method code: %d\n", req.code);
        break;
    }

    // send response (using dynamic buffer sized to payload to avoid truncation for GET all)
    size_t out_size = (size_t)resp.payload_len + 512; // overhead for header/options
    uint8_t *out = malloc(out_size);
    if (out)
    {
        int len = coap_serialize(&resp, out, out_size);
        if (len > 0)
        {
            sendto(task->sock, (const char *)out, len, 0,
                   (struct sockaddr *)&task->client_addr, task->addr_len);
        }
        else
        {
            fprintf(task->log_file,
                    "coap_serialize failed (out_size=%zu payload_len=%zu)\n",
                    out_size, (size_t)resp.payload_len);
        }
        free(out);
    }
    else
    {
        fprintf(task->log_file,
                "malloc failed for out buffer (size=%zu)\n", out_size);
    }

    fprintf(task->log_file,
            "Processed MID=%u Code=%u Uri=%s Response=%d\n",
            req.message_id, req.code,
            uri_path ? uri_path : "(none)",
            resp.code);
    fflush(task->log_file);

    // Free memory
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

    if (db_init(db_path) != 0)
    {
        fprintf(stderr, "Error initializing database: %s\n", db_path);
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

    db_close();
    return EXIT_SUCCESS;
}