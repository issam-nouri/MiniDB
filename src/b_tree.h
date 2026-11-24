#ifndef BTREE_H
#define BTREE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE       4096
#define MAX_INLINE_KEY  512
#define NULL_PAGE       0
#define MAX_TREE_DEPTH  16

typedef struct {
    uint32_t root_page_id;
    uint32_t next_page_id;
} FileHeader;

typedef struct {
    uint32_t block_num;
    uint16_t slot_num;
    uint16_t padding;
} DiskOffset;

typedef struct {
    uint8_t  is_leaf;
    uint8_t  padding1;
    uint16_t num_keys;
    uint16_t free_space_start;  // End of Slot Array (grows downward ->)
    uint16_t free_space_end;    // Start of Cell Area (<- grows upward)
    uint32_t right_link;        // Rightmost child page ID (NULL_PAGE if leaf)
    uint32_t padding2;
} PageHeader;

// Public B-Tree API
FILE* btree_open(const char *filename, FileHeader *file_hdr);
void  btree_close(FILE *fp, const FileHeader *file_hdr);
bool  btree_search(FILE *fp, const FileHeader *file_hdr, const char *key, DiskOffset *result);
bool  btree_insert(FILE *fp, FileHeader *file_hdr, const char *key, DiskOffset val);
bool  btree_delete(FILE *fp, FileHeader *file_hdr, const char *key);

#endif