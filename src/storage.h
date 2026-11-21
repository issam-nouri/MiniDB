#ifndef STORAGE_H
#define STORAGE_H

int storage_set(const char *key, int value);
int storage_get(const char *key, int *value);
void to_uppercase(char * str);

#endif