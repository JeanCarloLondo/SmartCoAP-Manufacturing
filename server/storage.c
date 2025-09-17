#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* Simple linked list node for storage */
typedef struct node
{
    int key;
    char *value; // malloc'd
    struct node *next;
} node_t;

static node_t *head = NULL;
static int next_id = 1;
static pthread_mutex_t storage_mutex = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

int storage_init(void)
{
    if (initialized)
        return 0;
    head = NULL;
    next_id = 1;
    pthread_mutex_init(&storage_mutex, NULL);
    initialized = 1;

    /* Add some simulated data */
    storage_add(0, "temp=25.3,hum=40.1");
    storage_add(0, "temp=24.8,hum=39.7");
    storage_add(0, "temp=26.0,hum=41.2");
    return 0;
}

void free_list_locked(void)
{
    node_t *cur = head;
    while (cur)
    {
        node_t *n = cur->next;
        free(cur->value);
        free(cur);
        cur = n;
    }
    head = NULL;
}

void storage_cleanup(void)
{
    pthread_mutex_lock(&storage_mutex);
    free_list_locked();
    pthread_mutex_unlock(&storage_mutex);
    pthread_mutex_destroy(&storage_mutex);
    initialized = 0;
}

int storage_add(int key, const char *value)
{
    if (!value)
        return -1;
    pthread_mutex_lock(&storage_mutex);
    if (key == 0)
    {
        key = next_id++;
    }
    else
    {
        /* If key specified, check uniqueness */
        node_t *it = head;
        while (it)
        {
            if (it->key == key)
            {
                pthread_mutex_unlock(&storage_mutex);
                return -1;
            }
            it = it->next;
        }
    }
    node_t *n = malloc(sizeof(node_t));
    if (!n)
    {
        pthread_mutex_unlock(&storage_mutex);
        return -1;
    }
    n->key = key;
    n->value = strdup(value);
    n->next = head;
    head = n;
    pthread_mutex_unlock(&storage_mutex);
    return key;
}

char *storage_get(int key)
{
    pthread_mutex_lock(&storage_mutex);

    if (key > 0)
    {
        node_t *it = head;
        while (it)
        {
            if (it->key == key)
            {
                char *out = strdup(it->value); // duplica la cadena
                pthread_mutex_unlock(&storage_mutex);
                return out; // caller debe liberar
            }
            it = it->next;
        }
        pthread_mutex_unlock(&storage_mutex);
        return NULL; // no encontrado
    }
    else
    {
        /* Build JSON-like array: [{"key":1,"value":"..."},...] */
        size_t cap = 256;
        char *out = malloc(cap);
        if (!out)
        {
            pthread_mutex_unlock(&storage_mutex);
            return NULL;
        }
        size_t len = 0;
        len += snprintf(out + len, cap - len, "[");

        node_t *it = head;
        int first = 1;
        while (it)
        {
            const char *fmt = first
                                  ? "{\"key\":%d,\"value\":\"%s\"}"
                                  : ",{\"key\":%d,\"value\":\"%s\"}";
            int need = snprintf(NULL, 0, fmt, it->key, it->value) + 1;
            if (len + need + 2 > cap)
            {
                cap = (cap + need) * 2;
                out = realloc(out, cap);
            }
            len += snprintf(out + len, cap - len, fmt, it->key, it->value);
            first = 0;
            it = it->next;
        }
        len += snprintf(out + len, cap - len, "]");
        pthread_mutex_unlock(&storage_mutex);
        return out; // caller libera
    }
}

int storage_update(int key, const char *value)
{
    if (key <= 0 || !value)
        return -1;
    pthread_mutex_lock(&storage_mutex);
    node_t *it = head;
    while (it)
    {
        if (it->key == key)
        {
            char *newv = strdup(value);
            if (!newv)
            {
                pthread_mutex_unlock(&storage_mutex);
                return -1;
            }
            free(it->value);
            it->value = newv;
            pthread_mutex_unlock(&storage_mutex);
            return 0;
        }
        it = it->next;
    }
    pthread_mutex_unlock(&storage_mutex);
    return -1; // not found
}

int storage_delete(int key)
{
    if (key <= 0)
        return -1;
    pthread_mutex_lock(&storage_mutex);
    node_t *prev = NULL;
    node_t *it = head;
    while (it)
    {
        if (it->key == key)
        {
            if (prev)
                prev->next = it->next;
            else
                head = it->next;
            free(it->value);
            free(it);
            pthread_mutex_unlock(&storage_mutex);
            return 0;
        }
        prev = it;
        it = it->next;
    }
    pthread_mutex_unlock(&storage_mutex);
    return -1; // not found
}