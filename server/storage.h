#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

/* Initialize storage. Returns 0 on success. */
int storage_init(void);

/* Cleanup storage, free all memory. */
void storage_cleanup(void);

/* Add a record.
 * If id == 0 -> auto-assign. Returns assigned id (>0) or -1 on error.
 * If key > 0 and already exists -> returns -1 (failure).
 */
int storage_add(int key, const char *value);

/* Get record(s):
 * If key > 0 -> returns malloc'd string with the value (caller must free).
 * If key == 0 -> returns malloc'd JSON-like array string of all records.
 * Returns NULL if not found or on error.
 */
char* storage_get(int key);

/* Update an existing record. Returns 0 success, -1 not found */
int storage_update(int key, const char *value);

/* Delete a record. Returns 0 success, -1 not found */
int storage_delete(int key);

#endif // STORAGE_H