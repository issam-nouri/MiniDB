
#ifndef LSM_TREE_H
#define LSM_TREE_H

#include <stddef.h>

#define LSM_KEY_SIZE 50

/* Un element de la MemTable */
typedef struct MemTableNode {
    char key[LSM_KEY_SIZE];
    int value;
    struct MemTableNode *next;
} MemTableNode;

/* La MemTable en memoire RAM */
typedef struct MemTable {
    MemTableNode *head;
    size_t count;
} MemTable;

/* Le moteur LSM-Tree */
typedef struct LSMTree {
    MemTable memtable;
} LSMTree;

/* Initialisation */
int lsm_tree_init(LSMTree *tree);

/* Insertion ou mise a jour */
int lsm_tree_set(LSMTree *tree, const char *key, int value);

/* Recherche */
int lsm_tree_get(const LSMTree *tree, const char *key, int *value);

/* Liberation de la memoire */
void lsm_tree_close(LSMTree *tree);

#endif
