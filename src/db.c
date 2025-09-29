#define _POSIX_C_SOURCE 200809L
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sqlite3 *db = NULL;

int db_init(const char *filename)
{
    if (sqlite3_open(filename, &db) != SQLITE_OK)
    {
        fprintf(stderr, "Error opening DB: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    const char *sql =
        "CREATE TABLE IF NOT EXISTS data ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "value TEXT NOT NULL,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
    char *errmsg = NULL;
    if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK)
    {
        fprintf(stderr, "Error creating table: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

int db_insert(const char *value)
{
    const char *sql = "INSERT INTO data (value) VALUES (?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return (int)sqlite3_last_insert_rowid(db);  // autoincrement
}

char *db_get_all(void)
{
    const char *sql = "SELECT id,value,timestamp FROM data;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    len += snprintf(out + len, cap - len, "[");

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *val = sqlite3_column_text(stmt, 1);
        const unsigned char *ts = sqlite3_column_text(stmt, 2);
        char item[256];
        int n = snprintf(item, sizeof(item),
                         "%s{\"id\":%d,\"value\":\"%s\",\"ts\":\"%s\"}",
                         first ? "" : ",", id, val, ts);
        if (len + n + 2 > cap)
        {
            cap = (cap + n) * 2;
            out = realloc(out, cap);
        }
        memcpy(out + len, item, n);
        len += n;
        first = 0;
    }
    len += snprintf(out + len, cap - len, "]");
    sqlite3_finalize(stmt);
    return out;
}

char *db_get_by_id(int id)
{
    const char *sql = "SELECT value,timestamp FROM data WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_int(stmt, 1, id);
    char *out = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char *val = sqlite3_column_text(stmt, 0);
        const unsigned char *ts = sqlite3_column_text(stmt, 1);
        out = malloc(256);
        if (out)
            snprintf(out, 256,
                     "{\"id\":%d,\"value\":\"%s\",\"ts\":\"%s\"}", id, val, ts);
    }
    sqlite3_finalize(stmt);
    return out;
}

int db_update(int id, const char *value)
{
    const char *sql = "UPDATE data SET value=? WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE && sqlite3_changes(db) > 0) ? 0 : -1;
}

int db_delete(int id)
{
    const char *sql = "DELETE FROM data WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE && sqlite3_changes(db) > 0) ? 0 : -1;
}

void db_close(void)
{
    if (db)
    {
        sqlite3_close(db);
        db = NULL;
    }
}