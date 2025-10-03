#ifndef DB_H
#define DB_H

#include <sqlite3.h>

/* -------------------------
   Database initialization
   ------------------------- */
int db_init(const char *filename);

/* -------------------------
   Insert functions
   ------------------------- */
int db_insert(const char *value);

int db_insert_with_id(int id, const char *value);

int db_insert_with_sensor(int sensor, const char *value);

/* -------------------------
   Read functions
   ------------------------- */

/* Return the raw stored 'value' (no JSON envelope). Caller must free. */
char *db_get_raw_by_id(int id);

/* Update only one field inside the stored JSON-like value (temp or hum).
   If row doesn't exist return -1. On success return 0.
   Strategy: read raw, parse temp and hum, replace the specified field and write back a clean JSON:
     {"temp":<x.xx>,"hum":<y.yy>}
*/
int db_update_field_in_json(int id, const char *field, const char *new_value);

char *db_get_all(void);

/* Return JSON object for a specific id {"id":x, "value":..., "ts":...} */
char *db_get_by_id(int id);

/* -------------------------
   Update & Delete functions
   ------------------------- */
int db_update(int id, const char *value);

int db_delete(int id);

void db_close(void);

#endif // DB_H