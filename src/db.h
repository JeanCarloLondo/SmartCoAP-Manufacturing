#ifndef DB_H
#define DB_H

#include <sqlite3.h>

int db_init(const char *filename);

int db_insert(const char *value);

int db_insert_with_id(int id, const char *value);

int db_insert_with_sensor(int sensor, const char *value);

char *db_get_raw_by_id(int id); /* return malloc'd value (no json), or NULL */

int db_update_field_in_json(int id, const char *field, const char *new_value);

char *db_get_all(void);

char *db_get_by_id(int id);

int db_update(int id, const char *value);

int db_delete(int id);

void db_close(void);

#endif // DB_H