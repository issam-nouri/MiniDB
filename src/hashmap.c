#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hashmap.h"

/* =========================================
   1. INITIALISATION
   ========================================= */

void hashmap_init(HashMap *map)
{
    if (map == NULL)
    {
        return;
    }

    for (int i = 0; i < HASHMAP_CAPACITY; i++)
    {
        map->buckets[i] = NULL;
    }
}


/* =========================================
   2. FONCTION DE HACHAGE
   ========================================= */

unsigned int hashmap_hash(const char *key)
{
    unsigned long hash = 5381;

    if (key == NULL)
    {
        return 0;
    }

    while (*key != '\0')
    {
        hash = hash * 33 + (unsigned char)*key;
        key++;
    }

    return (unsigned int)(hash % HASHMAP_CAPACITY);
}


/* =========================================
   3. INSERTION / MISE A JOUR
   ========================================= */

int hashmap_put(HashMap *map, const char *key, long offset)
{
    if (map == NULL || key == NULL)
    {
        return 0;
    }

    if (strlen(key) >= HASHMAP_KEY_SIZE)
    {
        return 0;
    }

    unsigned int index = hashmap_hash(key);

    HashEntry *current = map->buckets[index];

    /* Vérifier si la clé existe déjà */
    while (current != NULL)
    {
        if (strcmp(current->key, key) == 0)
        {
            /* La clé existe : on actualise son offset */
            current->offset = offset;

            return 1;
        }

        current = current->next;
    }

    /* La clé n'existe pas : créer une nouvelle entrée */
    HashEntry *new_entry = malloc(sizeof(HashEntry));

    if (new_entry == NULL)
    {
        return 0;
    }

    strcpy(new_entry->key, key);

    new_entry->offset = offset;

    /* Insertion en tête de la liste */
    new_entry->next = map->buckets[index];

    map->buckets[index] = new_entry;

    return 1;
}


/* =========================================
   4. RECHERCHE
   ========================================= */

int hashmap_get(HashMap *map, const char *key, long *offset)
{
    if (map == NULL || key == NULL || offset == NULL)
    {
        return 0;
    }

    unsigned int index = hashmap_hash(key);

    HashEntry *current = map->buckets[index];

    /* Parcourir la liste du bucket */
    while (current != NULL)
    {
        if (strcmp(current->key, key) == 0)
        {
            *offset = current->offset;

            return 1;
        }

        current = current->next;
    }

    /* Clé introuvable */
    return 0;
}


/* =========================================
   5. LIBERATION DE LA MEMOIRE
   ========================================= */

void hashmap_destroy(HashMap *map)
{
    if (map == NULL)
    {
        return;
    }

    for (int i = 0; i < HASHMAP_CAPACITY; i++)
    {
        HashEntry *current = map->buckets[i];

        while (current != NULL)
        {
            HashEntry *next = current->next;

            free(current);

            current = next;
        }

        map->buckets[i] = NULL;
    }
}