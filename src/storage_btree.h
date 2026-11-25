#ifndef STORAGE_BTREE_H
#define STORAGE_BTREE_H

#include "b_tree.h"

FILE * storage_btree_open();

int storage_btree_set(FILE * fp, const char *key, int value);
int storage_btree_get(FILE * fp, const char *key, int *value);

void storage_btree_close(FILE * fp);

#endif
