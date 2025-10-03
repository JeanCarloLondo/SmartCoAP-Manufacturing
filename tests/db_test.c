#include <stdio.h>
#include <stdlib.h>
#include "../src/db.h"

int main(void) {
    const char *db_file = "test_coap.db";

    // Initialize database
    if (db_init(db_file) != 0) 
    {
        fprintf(stderr, "Error initializing database: %s\n", db_file);
        return 1;
    }

    // Insert test records
    int id1 = db_insert("Hello world");
    if (id1 <= 0) 
    {
        fprintf(stderr, "Error inserting 'Hello world'\n");
    }

    int id2 = db_insert("Temperature=24");
    if (id2 <= 0) 
    {
        fprintf(stderr, "Error inserting 'Temperature=24'\n");
    }

    printf("Inserted: id1=%d, id2=%d\n", id1, id2);

    // Retrieve all records
    char *json = db_get_all();
    if (json) 
    {
        printf("Contents of table 'data':\n%s\n", json);
        free(json);
    } 
    else 
    {
        printf("Could not retrieve contents of table 'data' or it is empty.\n");
    }

    // Delete a record
    if (db_delete(id1) == 0) 
    {
        printf("Deleted record with id=%d\n", id1);
    } 
    else 
    {
        printf("Error deleting record with id=%d\n", id1);
    }

    // Retrieve all records again
    json = db_get_all();

    if (json) 
    {
        printf("Contents of table 'data':\n%s\n", json);
        free(json);
    } 
    else 
    {
        printf("Could not retrieve contents of table 'data' or it is empty.\n");
    }

    // Close database
    db_close();

    return 0;
}