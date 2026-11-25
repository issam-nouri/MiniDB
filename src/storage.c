
#include "storage.h"
#include "storage_linear.h"
#include "storage_hashmap.h"
#include "storage_btree.h"

static StorageMode active_mode = STORAGE_LINEAR;
static int initialized = 0;

/* Initialiser le moteur choisi */
int storage_init(StorageMode mode)
{
    if (initialized)
        return 0;

    if (mode == STORAGE_HASHMAP)
    {
        if (!storage_hashmap_init())
            return 0;
    }
    else if (mode != STORAGE_LINEAR)
    {
        return 0;
    }

    active_mode = mode;
    initialized = 1;

    return 1;
}

/* SET : choisir l'algorithme */
int storage_set(const char *key, int value)
{
    if (!initialized)
        return 0;

    if (active_mode == STORAGE_HASHMAP)
        return storage_hashmap_set(key, value);

    if(active_mode == STORAGE_BTREE)
    {
        FILE * fp = storage_btree_open();
        int result = storage_btree_set(fp, key, value);
        storage_btree_close(fp);
        return result;
    }

    return storage_linear_set(key, value);
}

/* GET : choisir l'algorithme */
int storage_get(const char *key, int *value)
{
    if (!initialized)
        return 0;

    if (active_mode == STORAGE_HASHMAP)
        return storage_hashmap_get(key, value);

    if(active_mode == STORAGE_BTREE)
    {
        FILE * fp = storage_btree_open();
        int result = storage_btree_get(fp, key, value);
        storage_btree_close(fp);
        return result;
    }

    return storage_linear_get(key, value);
}

/* Fermer le moteur */
void storage_close(void)
{
    if (!initialized)
        return;

    if (active_mode == STORAGE_HASHMAP)
        storage_hashmap_close();

    initialized = 0;
}
