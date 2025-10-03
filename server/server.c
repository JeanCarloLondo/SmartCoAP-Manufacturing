#include "coap.h"

#include "../src/db.h"              // SQLite database helper functions
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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

// Task structure representing a single client request
typedef struct
{
    int sock;                       // Server UDP socket
    struct sockaddr_in client_addr; // Client address
    socklen_t addr_len;
    uint8_t buffer[BUF_SIZE];       // Incoming CoAP datagram
    ssize_t msg_len;                // Length of datagram
    FILE *log_file;                 // Pointer to log file
} client_task_t;

/* ------------------------
   Logging helper
   ------------------------ */
// Writes log messages with timestamp and level (INFO/ERROR).
static void log_message(FILE *logf, const char *level, const char *fmt, ...) {
    if (!logf) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", t);

    fprintf(logf, "[%s] %s: ", tbuf, level);

    va_list args;
    va_start(args, fmt);
    vfprintf(logf, fmt, args);
    va_end(args);

    fprintf(logf, "\n");
    fflush(logf);
}

/* ------------------------
   Response initializer
   ------------------------ */
// Creates a response template based on a request: copies MID, token, and sets type.
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

/* ------------------------
   Utility helpers
   ------------------------ */
// Trim whitespace in place from both ends of a string
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
// Extract a numeric value following a keyword like "temp" or "hum" from a string
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

// Extract Uri-Path options (number 11) and join them into a string like "sensor/1"
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

// Return 1 if string is a number, 0 otherwise
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

// Parse URIs of form "sensor/<id>", returning the numeric id, or -1 if not valid
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

/* ------------------------
   Worker thread
   ------------------------ */
// Each incoming CoAP datagram spawns this handler in its own thread.
// Parses request, executes CRUD on SQLite, builds response, and sends it back.
#if defined(_WIN32) || defined(_WIN64)
DWORD WINAPI handle_client(LPVOID arg)
#else
void *handle_client(void *arg)
#endif
{
    client_task_t *task = (client_task_t *)arg;

    // Parse request
    coap_message_t req;
    memset(&req, 0, sizeof(req));

    if (coap_parse(task->buffer, (size_t)task->msg_len, &req) != COAP_OK)
    {
        log_message(task->log_file, "ERROR", "Failed to parse CoAP message\n");
        free(task);
#if defined(_WIN32) || defined(_WIN64)
        return 0;
#else
        return NULL;
#endif
    }

    // Prepare response template
    coap_message_t resp;
    init_response_from_request(&req, &resp);

    char *uri_path = extract_uri_path(&req);  // Extract Uri-Path string
    char tmpbuf[1024];

    // Dispatch by request method
    switch (req.code)
    {
    // GET: retrieve single record or all records
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
                log_message(task->log_file, "INFO", "GET id=%d: Found", id);
            }
            else
            {
                resp.code = COAP_CODE_NOT_FOUND;
                log_message(task->log_file, "ERROR", "GET id=%d: Not found", id);
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
                log_message(task->log_file, "INFO", "GET all: Success");
            }
            else
            {
                resp.code = COAP_CODE_INTERNAL_ERROR;
                log_message(task->log_file, "ERROR", "GET all: Database error");
            }
        }
        break;
    }
    // POST: insert new record (auto-id, explicit id, or sensor-specific)
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
                        log_message(task->log_file, "INFO", "POST: Created id=%d (explicit)", id);
                    }
                    else
                    {
                        resp.code = COAP_CODE_BAD_REQUEST;
                        log_message(task->log_file, "ERROR", "POST: explicit id=%d insert failed", explicit_id);
                    }
                }
                else
                {
                    resp.code = COAP_CODE_BAD_REQUEST;
                    log_message(task->log_file, "ERROR", "POST: explicit id provided but no value");
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
                    log_message(task->log_file, "INFO", "POST: Created id=%d (sensor=%d)", id, sensor_id);
                }
                else
                {
                    resp.code = COAP_CODE_INTERNAL_ERROR;
                    log_message(task->log_file, "ERROR", "POST: sensor insert failed (sensor=%d)", sensor_id);
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
                    log_message(task->log_file, "INFO", "POST: Created id=%d", id);
                }
                else
                {
                    resp.code = COAP_CODE_INTERNAL_ERROR;
                    log_message(task->log_file, "ERROR", "POST: Database insert failed");
                }
            }
        }
        else
        {
            resp.code = COAP_CODE_BAD_REQUEST;
            log_message(task->log_file, "ERROR", "POST: Empty payload");
        }
        break;
    }
    // PUT: update record by id (partial update temp/hum, or full replace)
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
                    log_message(task->log_file, "INFO", "PUT: Updated id=%d (temp+hum)", id);
                }
                else
                {
                    resp.code = COAP_CODE_NOT_FOUND;
                    log_message(task->log_file, "ERROR", "PUT: id=%d not found (temp+hum)\n", id);
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
                        log_message(task->log_file, "INFO", "PUT: Updated temp id=%d", id);
                    }
                    else
                    {
                        resp.code = COAP_CODE_NOT_FOUND;
                        log_message(task->log_file, "ERROR", "PUT: id=%d not found (temp)", id);
                    }
                }
                else
                {
                    resp.code = COAP_CODE_BAD_REQUEST;
                    log_message(task->log_file, "ERROR", "PUT: temp value parse error for id=%d", id);
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
                        log_message(task->log_file, "INFO", "PUT: Updated hum id=%d", id);
                    }
                    else
                    {
                        resp.code = COAP_CODE_NOT_FOUND;
                        log_message(task->log_file, "ERROR", "PUT: id=%d not found (hum)", id);
                    }
                }
                else
                {
                    resp.code = COAP_CODE_BAD_REQUEST;
                    log_message(task->log_file, "ERROR", "PUT: hum value parse error for id=%d", id);
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
                    log_message(task->log_file, "INFO", "PUT: Updated id=%d (full replace)", id);
                }
                else
                {
                    resp.code = COAP_CODE_NOT_FOUND;
                    log_message(task->log_file, "ERROR", "PUT: id=%d not found (full)", id);
                }
            }

            free(payload);
        }
        else
        {
            resp.code = COAP_CODE_BAD_REQUEST;
            log_message(task->log_file, "ERROR", "PUT: Invalid format (expected: id=value)");
        }
        break;
    }
    // DELETE: delete record by id
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
            log_message(task->log_file, "INFO", "DELETE: Deleted id=%d", id);
        }
        else
        {
            resp.code = COAP_CODE_NOT_FOUND;
            log_message(task->log_file, "ERROR", "DELETE: id=%d not found or invalid", id);
        }
        break;
    }
    default:
        resp.code = COAP_CODE_BAD_REQUEST;
        log_message(task->log_file, "ERROR", "Unsupported method code: %d", req.code);
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
            log_message(task->log_file, "ERROR", "coap_serialize failed (out_size=%zu payload_len=%zu)\n",
                    out_size, (size_t)resp.payload_len);
        }
        free(out);
    }
    else
    {
        log_message(task->log_file, "ERROR", "malloc failed for out buffer (size=%zu)", out_size);
    }

    // Log summary
    log_message(task->log_file, "INFO", "Processed MID=%u Code=%u Uri=%s Response=%d",
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

/* ------------------------
   Main function
   ------------------------ */
// Initializes database, opens UDP socket, and enters infinite loop
// spawning threads to handle incoming CoAP datagrams.
int main(int argc, char *argv[])
{
    // Database setup
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

    // Port and logging setup
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

    // Networking setup (cross-platform)
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

    // Main loop: receive datagrams, spawn worker threads
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
            log_message(logf, "ERROR", "CreateThread failed");
            free(task);
            continue;
        }
        CloseHandle(tid);
#else
        thread_t tid;
        if (pthread_create(&tid, NULL, handle_client, task) != 0)
        {
            log_message(logf, "ERROR", "pthread_create failed");
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