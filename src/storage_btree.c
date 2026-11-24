#include "storage_btree.h"

#define DATA_FILE "data.tdb"

static FileHeader hdr;

FILE * storage_btree_open()
{
    return btree_open(DATA_FILE, &hdr);
}

int storage_btree_set(FILE * fp, const char *key, DiskOffset value)
{
    return btree_insert(fp, &hdr, key, value);
}
int storage_btree_get(FILE * fp, const char *key, DiskOffset *value)
{
    return btree_search(fp, &hdr, key, value);
}

void storage_btree_close(FILE * fp)
{
    return btree_close(fp, &hdr);
}