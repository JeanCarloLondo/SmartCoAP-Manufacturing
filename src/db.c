#define _POSIX_C_SOURCE 200809L
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>

static sqlite3 *db = NULL; // Global SQLite connection handle

/* -------------------------
   Database initialization
   ------------------------- */
// Opens (or creates) the SQLite database file.
// Also creates a table `data` if it does not exist yet.
// Columns: id (autoincrement), sensor id, value text, timestamp (default = current localtime).
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
        "sensor INTEGER DEFAULT 0,"
        "value TEXT NOT NULL,"
        "timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%S','now','localtime'))"
        ");";
    char *errmsg = NULL;
    if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK)
    {
        fprintf(stderr, "Error creating table: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

/* -------------------------
   Insert functions
   ------------------------- */

// Insert a record with only `value`. ID is auto-assigned.
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
    return (int)sqlite3_last_insert_rowid(db); // autoincrement
}

// Insert with explicit ID (useful for PUT/POST with client-specified id)
int db_insert_with_id(int id, const char *value)
{
    const char *sql = "INSERT INTO data (id, value) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return id;
}

/* Insert including sensor id */
// Insert with a specific sensor id (maps one record to a sensor)
int db_insert_with_sensor(int sensor, const char *value)
{
    const char *sql = "INSERT INTO data (sensor, value) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, sensor);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return (int)sqlite3_last_insert_rowid(db);
}

/* -------------------------
   Read functions
   ------------------------- */

// Return last 26 rows as a JSON array string.
// Rows are reversed to show oldest first.
char *db_get_all(void)
{
    // Get last 26 entries ordered by id desc (newest first)
    const char *sql =
        "SELECT id,value,timestamp FROM data "
        "ORDER BY id DESC LIMIT 26;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    // Prepare buffer
    size_t cap = 2048, len = 0;
    char *out = malloc(cap);
    if (!out)
        return NULL;

    len += snprintf(out + len, cap - len, "[\n");

    int first = 1;
    // save on a buffer first, then reverse order
    typedef struct
    {
        int id;
        char val[256];
        char ts[64];
    } row_t;
    row_t rows[26];
    int count = 0;

    // Read results (newest first)
    while (sqlite3_step(stmt) == SQLITE_ROW && count < 26)
    {
        rows[count].id = sqlite3_column_int(stmt, 0);
        snprintf(rows[count].val, sizeof(rows[count].val), "%s",
                 sqlite3_column_text(stmt, 1));
        snprintf(rows[count].ts, sizeof(rows[count].ts), "%s",
                 sqlite3_column_text(stmt, 2));
        count++;
    }
    sqlite3_finalize(stmt);

    // reverse order to have oldest first
    for (int i = count - 1; i >= 0; i--)
    {
        char item[512];
        int n = snprintf(item, sizeof(item),
                         "  %s{\"id\":%d, \"value\":\"%s\", \"ts\":\"%s\"}",
                         first ? "" : ",\n", rows[i].id, rows[i].val, rows[i].ts);
        if (len + n + 2 > cap)
        {
            cap = (cap + n) * 2;
            out = realloc(out, cap);
        }
        memcpy(out + len, item, n);
        len += n;
        first = 0;
    }

    len += snprintf(out + len, cap - len, "\n]");
    return out;
}

/* Return the raw stored 'value' (no JSON envelope). Caller must free. */
char *db_get_raw_by_id(int id)
{
    const char *sql = "SELECT value FROM data WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_int(stmt, 1, id);
    char *out = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char *val = sqlite3_column_text(stmt, 0);
        if (val)
        {
            size_t n = strlen((const char *)val) + 1;
            out = malloc(n);
            if (out)
                memcpy(out, val, n);
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

/* Return JSON object for a specific id {"id":x, "value":..., "ts":...} */
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
        out = malloc(512);
        if (out)
            snprintf(out, 512,
                     "{\"id\":%d, \"value\":\"%s\", \"ts\":\"%s\"}", id, val ? (const char *)val : "", ts ? (const char *)ts : "");
    }
    sqlite3_finalize(stmt);
    return out;
}

/* -------------------------
   Update & Delete functions
   ------------------------- */

// Update value for an id
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

// Delete record by id
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

/* Helper: parse numeric temp/hum values from a stored string.
   It looks for "temp":<number> and "hum":<number> anywhere in text.
   If not found sets out_temp/out_hum to HUGE_VAL (NaN-like sentinel).
*/
static void parse_temp_hum(const char *s, double *out_temp, double *out_hum)
{
    const char *p;
    char *end;
    *out_temp = HUGE_VAL;
    *out_hum = HUGE_VAL;

    if (!s)
        return;
    // Look for "temp":<number>
    p = strstr(s, "temp");
    if (p)
    {
        /* find ':' after temp */
        p = strchr(p, ':');
        if (p)
        {
            p++;
            while (*p && isspace((unsigned char)*p))
                p++;
            double v = strtod(p, &end);
            if (end != p)
                *out_temp = v;
        }
    }
    p = strstr(s, "hum");
    if (p)
    {
        p = strchr(p, ':');
        if (p)
        {
            p++;
            while (*p && isspace((unsigned char)*p))
                p++;
            double v = strtod(p, &end);
            if (end != p)
                *out_hum = v;
        }
    }
}

/* Update only one field inside the stored JSON-like value (temp or hum).
   If row doesn't exist return -1. On success return 0.
   Strategy: read raw, parse temp and hum, replace the specified field and write back a clean JSON:
     {"temp":<x.xx>,"hum":<y.yy>}
*/
int db_update_field_in_json(int id, const char *field, const char *new_value)
{
    if (!field || !new_value)
        return -1;

    char *raw = db_get_raw_by_id(id);
    if (!raw)
        return -1;

    double temp_v = HUGE_VAL, hum_v = HUGE_VAL;
    parse_temp_hum(raw, &temp_v, &hum_v);

    /* parse new_value as number if possible */
    char *endptr = NULL;
    double newnum = strtod(new_value, &endptr);
    if (endptr == new_value)
    {
        /* not a number; try to strip quotes and parse */
        const char *p = new_value;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '"' || *p == '\'')
            p++;
        newnum = strtod(p, &endptr);
    }

    if (strcasecmp(field, "temp") == 0)
    {
        if (endptr != new_value)
            temp_v = newnum;
    }
    else if (strcasecmp(field, "hum") == 0)
    {
        if (endptr != new_value)
            hum_v = newnum;
    }
    else
    {
        free(raw);
        return -1;
    }

    /* If any value still not found, set to 0.0 as fallback */
    if (!isfinite(temp_v))
        temp_v = 0.0;
    if (!isfinite(hum_v))
        hum_v = 0.0;

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"temp\":%.2f,\"hum\":%.2f}", temp_v, hum_v);

    int rc = db_update(id, buf);
    free(raw);
    return rc;
}

void db_close(void)
{
    if (db)
    {
        sqlite3_close(db);
        db = NULL;
    }
}