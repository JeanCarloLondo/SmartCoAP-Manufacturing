#ifndef DB_H
#define DB_H

#include <sqlite3.h>

int db_init(const char *filename);

int db_insert(const char *value);

char *db_get_all(void);

char *db_get_by_id(int id);

int db_update(int id, const char *value);

int db_delete(int id);

void db_close(void);

#endif // DB_H