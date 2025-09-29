#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <stdint.h>

int handle_client(const uint8_t *in_buf, size_t in_len, uint8_t **out_resp, size_t *out_resp_len);

#endif // SERVER_H
