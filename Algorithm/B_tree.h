#ifndef B_TREE_H
#define B_TREE_H
#include <stdbool.h>

// The Order of the tree (m). 
// Dictates maximum children (m) and maximum keys (m - 1).
#define ORDER 4  

// An expanded disk pointer struct instead of a single unsigned long
typedef struct {
    unsigned long block_num;
    unsigned short slot_num;
} DiskOffset;

typedef struct BNode {
    // 1. Metadata
    int num_keys;           // How many keys are currently in this node
    bool is_leaf;           // True if this is a bottom-level node (crucial for insertion)

    // 2. The Search Keys (max m - 1)
    char * keys[ORDER - 1];  

    // 3. The Data Payloads (max m - 1)
    // Every key needs its own exact disk offset.
    DiskOffset offsets[ORDER - 1];

    // 4. The Child Pointers (max m)
    // There is always one more child pointer than there are keys.
    struct BNode *children[ORDER]; 
} BNode, * BTree;

// Expanded function signatures with proper arguments
BTree init_tree();
void insert(BTree *root, char *key, DiskOffset offset);
DiskOffset* search(BTree root, char *key);
void delete(BTree *root, char *key);
void destroy_tree(BTree root);

#endif