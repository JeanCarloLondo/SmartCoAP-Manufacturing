
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <strings.h>

#include "../src/coap.h"

// Default server address and port
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 5683
#define MAX_BUF 2048         // Max CoAP message size
#define RECV_TIMEOUT_MS 2000 // 2 seconds

/* Generate a random CoAP message ID (MID) */
static uint16_t random_mid(void)
{
    return (uint16_t)(rand() & 0xFFFF);
}

/* Check if a string is purely numeric (for sensor IDs, etc.) */
static int is_numeric(const char *s)
{
    if (!s || *s == '\0')
        return 0;
    if (*s == '-') // allow negative numbers
        s++;
    while (*s)
    {
        if (*s < '0' || *s > '9')
            return 0;
        s++;
    }
    return 1;
}

/* 
   Send a CoAP message to the server and wait for a reply. 
   Uses select() to wait with a timeout. 
   Returns:
     0 -> success
     1 -> timeout
    -1 -> send error
    -3 -> parse error
    -4 -> receive error
*/
static int send_coap_and_wait(int sock, struct sockaddr_in *srv,
                              coap_message_t *msg, int timeout_ms)
{
    uint8_t out[MAX_BUF];
    int outlen = coap_serialize(msg, out, sizeof(out));
    if (outlen <= 0)
        return -1;

    if (sendto(sock, out, outlen, 0, (struct sockaddr *)srv, sizeof(*srv)) < 0)
        return -1;

    // Prepare select() for timeout
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rv = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (rv > 0 && FD_ISSET(sock, &rfds))
    {
        // Got response -> read it
        uint8_t in[MAX_BUF];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t r = recvfrom(sock, in, sizeof(in), 0, (struct sockaddr *)&from, &flen);
        if (r > 0)
        {
            coap_message_t resp;
            if (coap_parse(in, r, &resp) == COAP_OK)
            {
                printf("<< Received: Type=%u MID=%u Code=0x%02X\n",
                       (unsigned)resp.type, resp.message_id, resp.code);
                if (resp.payload_len > 0)
                {
                    printf("<< Payload: %.*s\n", (int)resp.payload_len, (char *)resp.payload);
                }
                coap_free_message(&resp);
                return 0;
            }
            return -3; // parse error
        }
        return -4; // recv error
    }
    return 1; // timeout
}

/* Print usage instructions */
static void usage(const char *me)
{
    printf("Usage: %s [server_ip] [server_port] [sensor_number] [message_id]\n", me);
    printf("  If omitted, defaults: %s %d <no-sensor> <random-mid>\n", DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT);
    printf("\nCommands (interactive):\n");
    printf("  GET [id|all]        -> GET specific id (number) or all (no arg or 'all')\n");
    printf("  PUT id=value        -> Update record with id to value (payload 'id=value')\n");
    printf("  DELETE id           -> Delete record with id (payload 'id')\n");
    printf("  POST value          -> Insert new record (sends POST payload=value). If sensor_number was given it will add Uri-Path 'sensor/<n>'\n");
    printf("  exit                -> quit\n");
    printf("\nExamples:\n  GET 3\n  PUT 3=temperature:22.5\n  DELETE 3\n  POST {\"temp\":22}\n");
}

/* -------------------
   Main entry point
   ------------------- */
int main(int argc, char **argv)
{
    srand(time(NULL) ^ getpid());

    // Parse command line arguments
    const char *server_ip = argc > 1 ? argv[1] : DEFAULT_SERVER_IP;
    int server_port = argc > 2 ? atoi(argv[2]) : DEFAULT_SERVER_PORT;
    const char *sensor_number = argc > 3 ? argv[3] : NULL;
    uint16_t fixed_mid = (argc > 4 ? (uint16_t)atoi(argv[4]) : 0);

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    // Server address
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &srv.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server IP\n");
        close(sock);
        return 1;
    }

    printf("CoAP console-client -> server %s:%d\n", server_ip, server_port);
    if (sensor_number)
        printf("Default sensor number: %s\n", sensor_number);
    if (fixed_mid)
        printf("Using fixed message-id: %u (use 0 for random per-request)\n", fixed_mid);

    char line[1024];
    usage(argv[0]);

    /* -------------------
       Interactive loop
       ------------------- */
    while (1)
    {
        printf("\ncoap> ");
        if (!fgets(line, sizeof(line), stdin))
            break;
        // trim newline
        size_t L = strlen(line);
        if (L && (line[L - 1] == '\n' || line[L - 1] == '\r'))
            line[--L] = '\0';
        if (L == 0)
            continue;

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
            break;

        // parse command
        char cmd[16];
        char arg1[512];
        arg1[0] = '\0';
        int n = sscanf(line, "%15s %511[^\n]", cmd, arg1);

        if (n < 1)
            continue;

        /* --------- GET --------- */
        if (strcasecmp(cmd, "GET") == 0)
        {
            coap_message_t msg;
            coap_init_message(&msg);
            msg.version = COAP_VERSION;
            msg.type = COAP_TYPE_CON;
            msg.code = COAP_CODE_GET;
            msg.message_id = fixed_mid ? fixed_mid : random_mid();

            // If arg1 provided and not "all" -> add uri-path
            if (n == 2 && strcmp(arg1, "all") != 0)
            {
                char pathbuf[64];
                // If arg1 is numeric, send as sensor/<arg1> to be consistent with simulators
                if (is_numeric(arg1))
                {
                    snprintf(pathbuf, sizeof(pathbuf), "sensor/%s", arg1);
                    coap_add_option(&msg, 11, (uint8_t *)"sensor", strlen("sensor"));
                    coap_add_option(&msg, 11, (uint8_t *)arg1, strlen(arg1));
                    printf(">> Sending GET MID=%u Uri=sensor/%s\n", msg.message_id, arg1);
                }
                else
                {
                    // non-numeric path (allow other paths)
                    coap_add_option(&msg, 11, (uint8_t *)arg1, strlen(arg1));
                    printf(">> Sending GET MID=%u Uri=%s\n", msg.message_id, arg1);
                }
            }
            else
            {
                printf(">> Sending GET MID=%u Uri=(all)\n", msg.message_id);
            }

            int rc = send_coap_and_wait(sock, &srv, &msg, RECV_TIMEOUT_MS);
            if (rc == 1)
                printf("!! timeout (no ACK)\n");
            else if (rc < 0)
                printf("!! send error rc=%d\n", rc);
        }

        /* --------- POST --------- */
        else if (strcasecmp(cmd, "POST") == 0)
        {
            if (n < 2)
            {
                printf("POST requires payload\n");
                continue;
            }
            coap_message_t msg;
            coap_init_message(&msg);
            msg.version = COAP_VERSION;
            msg.type = COAP_TYPE_CON;
            msg.code = COAP_CODE_POST;
            msg.message_id = fixed_mid ? fixed_mid : random_mid();

            // If sensor_number exists, create Uri-Path "sensor" and "<sensor_number>" so path = sensor/<n>
            if (sensor_number)
            {
                coap_add_option(&msg, 11, (uint8_t *)"sensor", strlen("sensor"));
                coap_add_option(&msg, 11, (uint8_t *)sensor_number, strlen(sensor_number));
            }
            msg.payload = (uint8_t *)arg1;
            msg.payload_len = strlen(arg1);

            printf(">> Sending POST MID=%u Uri=%s Payload=%s\n", msg.message_id,
                   sensor_number ? "sensor/<id>" : "(none)", arg1);
            int rc = send_coap_and_wait(sock, &srv, &msg, RECV_TIMEOUT_MS);
            if (rc == 1)
                printf("!! timeout (no ACK)\n");
            else if (rc < 0)
                printf("!! send error rc=%d\n", rc);
        }

        /* --------- PUT --------- */
        else if (strcasecmp(cmd, "PUT") == 0)
        {
            if (n < 2)
            {
                printf("PUT requires id=value\n");
                continue;
            }
            char *eq = strchr(arg1, '=');
            if (!eq)
            {
                printf("PUT format: id=value\n");
                continue;
            }

            coap_message_t msg;
            coap_init_message(&msg);
            msg.version = COAP_VERSION;
            msg.type = COAP_TYPE_CON;
            msg.code = COAP_CODE_PUT;
            msg.message_id = fixed_mid ? fixed_mid : random_mid();

            // If id=value and id is numeric, put as sensor/<id> Uri-Path for consistency
            char idbuf[64];
            size_t idlen = (size_t)(eq - arg1);
            if (idlen < sizeof(idbuf))
            {
                memcpy(idbuf, arg1, idlen);
                idbuf[idlen] = '\0';
            }
            else
            {
                idbuf[0] = '\0';
            }

            if (is_numeric(idbuf))
            {
                // Build payload as "id=value" as before, but add Uri Path sensor/<id>
                coap_add_option(&msg, 11, (uint8_t *)"sensor", strlen("sensor"));
                coap_add_option(&msg, 11, (uint8_t *)idbuf, strlen(idbuf));
            }

            msg.payload = (uint8_t *)arg1;
            msg.payload_len = strlen(arg1);

            printf(">> Sending PUT MID=%u payload=%s\n", msg.message_id, arg1);
            int rc = send_coap_and_wait(sock, &srv, &msg, RECV_TIMEOUT_MS);
            if (rc == 1)
                printf("!! timeout (no ACK)\n");
            else if (rc < 0)
                printf("!! send error rc=%d\n", rc);
        }

        /* --------- DELETE --------- */
        else if (strcasecmp(cmd, "DELETE") == 0)
        {
            if (n < 2)
            {
                printf("DELETE requires id\n");
                continue;
            }

            coap_message_t msg;
            coap_init_message(&msg);
            msg.version = COAP_VERSION;
            msg.type = COAP_TYPE_CON;
            msg.code = COAP_CODE_DELETE;
            msg.message_id = fixed_mid ? fixed_mid : random_mid();

            // If id numeric, use sensor/<id> path
            if (is_numeric(arg1))
            {
                coap_add_option(&msg, 11, (uint8_t *)"sensor", strlen("sensor"));
                coap_add_option(&msg, 11, (uint8_t *)arg1, strlen(arg1));
            }

            msg.payload = (uint8_t *)arg1;
            msg.payload_len = strlen(arg1);

            printf(">> Sending DELETE MID=%u id=%s\n", msg.message_id, arg1);
            int rc = send_coap_and_wait(sock, &srv, &msg, RECV_TIMEOUT_MS);
            if (rc == 1)
                printf("!! timeout (no ACK)\n");
            else if (rc < 0)
                printf("!! send error rc=%d\n", rc);
        }
        else
        {
            printf("Unknown command: %s\n", cmd);
            usage(argv[0]);
        }
    }

    close(sock);
    return 0;
}