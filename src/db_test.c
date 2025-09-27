#include <stdio.h>
#include "db.h"

int main(void) {
    if (db_init("test_coap.db") != 0) {
        fprintf(stderr, "Error al inicializar la base de datos\n");
        return 1;
    }

    // Insertar datos en la tabla data
    int id1 = db_insert("Hola mundo");
    int id2 = db_insert("Temperatura=24");

    printf("Insertados id=%d y id=%d\n", id1, id2);

    // Leer todo
    char *json = db_get_all();
    if (json) {
        printf("Contenido de data:\n%s\n", json);
        free(json);
    }

    db_close();
    return 0;
}
