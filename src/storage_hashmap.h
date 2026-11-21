
#ifndef STORAGE_HASHMAP_H
#define STORAGE_HASHMAP_H

int storage_hashmap_init(void);

int storage_hashmap_set(const char *key, int value);
int storage_hashmap_get(const char *key, int *value);

void storage_hashmap_close(void);

#endif
