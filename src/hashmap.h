#ifndef HASHMAP_H
#define HASHMAP_H

#define HASHMAP_CAPACITY 101
#define HASHMAP_KEY_SIZE 50

typedef struct HashEntry
{
    char key[HASHMAP_KEY_SIZE];
    long offset;

    struct HashEntry *next;
} HashEntry;

typedef struct HashMap
{
    HashEntry *buckets[HASHMAP_CAPACITY];
} HashMap;

/* Initialisation */
void hashmap_init(HashMap *map);

/* Fonction de hachage */
unsigned int hashmap_hash(const char *key);

/* Insertion ou mise à jour */
int hashmap_put(HashMap *map, const char *key, long offset);

/* Recherche */
int hashmap_get(HashMap *map, const char *key, long *offset);

/* Libération de la mémoire */
void hashmap_destroy(HashMap *map);

#endif