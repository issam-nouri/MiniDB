#include "b_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_PAGE_SIZE 4096

typedef struct {
    uint8_t active;   // 1 = valid row, 0 = deleted (tombstone)
    char    name[64];
    int32_t age;
} Person;

#define SLOTS_PER_PAGE (DATA_PAGE_SIZE / sizeof(Person))

// Convert (block_num, slot_num) into a byte offset inside data.bin
static long offset_to_bytes(DiskOffset loc) {
    return (long)loc.block_num * DATA_PAGE_SIZE + (long)loc.slot_num * (long)sizeof(Person);
}

// 1. INSERT: write row to data.bin, then index its location in the B-Tree
bool db_insert(FILE *idx_fp, FileHeader *hdr, FILE *data_fp, const char *name, int age) {
    DiskOffset existing;
    if (btree_search(idx_fp, hdr, name, &existing)) {
        return false; // Reject duplicate name
    }

    // Find the next free slot at the end of data.bin
    fseek(data_fp, 0, SEEK_END);
    long file_bytes = ftell(data_fp);

    uint32_t block_num = (uint32_t)(file_bytes / DATA_PAGE_SIZE);
    uint16_t slot_num  = (uint16_t)((file_bytes % DATA_PAGE_SIZE) / sizeof(Person));

    // If the current 4KB block can't fit another Person, start a new block
    if (slot_num >= SLOTS_PER_PAGE) {
        block_num++;
        slot_num = 0;
    }

    DiskOffset loc = { .block_num = block_num, .slot_num = slot_num, .padding = 0 };

    Person row = {0};
    row.active = 1;
    strncpy(row.name, name, sizeof(row.name) - 1);
    row.age = age;

    // Write the row to data.bin
    fseek(data_fp, offset_to_bytes(loc), SEEK_SET);
    fwrite(&row, sizeof(Person), 1, data_fp);
    fflush(data_fp);

    // Index (name -> loc) in the B-Tree
    return btree_insert(idx_fp, hdr, row.name, loc);
}

// 2. SEARCH: look up DiskOffset in B-Tree, then read row from data.bin
bool db_search(FILE *idx_fp, const FileHeader *hdr, FILE *data_fp, const char *name, Person *out) {
    DiskOffset loc;
    if (!btree_search(idx_fp, hdr, name, &loc)) {
        return false;
    }

    fseek(data_fp, offset_to_bytes(loc), SEEK_SET);
    if (fread(out, sizeof(Person), 1, data_fp) != 1 || !out->active) {
        return false;
    }
    return true;
}

// 3. DELETE: remove from B-Tree, then mark row as inactive in data.bin
bool db_delete(FILE *idx_fp, FileHeader *hdr, FILE *data_fp, const char *name) {
    DiskOffset loc;
    if (!btree_search(idx_fp, hdr, name, &loc)) {
        return false;
    }

    // Remove from index first
    if (!btree_delete(idx_fp, hdr, name)) {
        return false;
    }

    // Mark tombstone in data.bin (only need to overwrite the 1-byte active flag)
    uint8_t inactive = 0;
    fseek(data_fp, offset_to_bytes(loc), SEEK_SET);
    fwrite(&inactive, sizeof(uint8_t), 1, data_fp);
    fflush(data_fp);
    return true;
}

int main(void) {
    FileHeader hdr;
    FILE *idx_fp = btree_open("index.db", &hdr);
    FILE *data_fp = fopen("data.bin", "r+b");
    if (!data_fp) data_fp = fopen("data.bin", "w+b");

    // Insert rows
    db_insert(idx_fp, &hdr, data_fp, "alice", 28);
    db_insert(idx_fp, &hdr, data_fp, "bob", 34);
    db_insert(idx_fp, &hdr, data_fp, "charlie", 22);

    // Search for a row
    Person p;
    if (db_search(idx_fp, &hdr, data_fp, "bob", &p)) {
        printf("Found: %s, age %d\n", p.name, p.age);
    }

    // Delete a row
    // if (db_delete(idx_fp, &hdr, data_fp, "bob")) {
    //     printf("Deleted bob\n");
    // }

    // if (!db_search(idx_fp, &hdr, data_fp, "bob", &p)) {
    //     printf("Verified: bob is gone\n");
    // }

    fclose(data_fp);
    btree_close(idx_fp, &hdr);
    return 0;
}