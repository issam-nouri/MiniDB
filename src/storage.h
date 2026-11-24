
#ifndef STORAGE_H
#define STORAGE_H

typedef enum
{
    STORAGE_LINEAR,
    STORAGE_HASHMAP,
    STORAGE_BTREE
} StorageMode;

int storage_init(StorageMode mode);

int storage_set(const char *key, int value);

int storage_get(const char *key, int *value);

void storage_close(void);

#endif
