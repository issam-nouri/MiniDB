#include "b_tree.h"
#include <stdlib.h>
#include <string.h>

#define UNDERFLOW_BYTES ((PAGE_SIZE - sizeof(PageHeader)) / 4)

typedef struct {
    char       key[MAX_INLINE_KEY];
    uint16_t   key_len;
    DiskOffset val;
    uint32_t   left_child;
} TempCell;

// ============================================================================
// 1. DISK I/O HELPERS (static)
// ============================================================================

static void write_file_header(FILE *fp, const FileHeader *hdr) {
    uint8_t page0[PAGE_SIZE] = {0};
    memcpy(page0, hdr, sizeof(FileHeader));
    fseek(fp, 0, SEEK_SET);
    fwrite(page0, PAGE_SIZE, 1, fp);
    fflush(fp);
}

static void read_file_header(FILE *fp, FileHeader *hdr) {
    fseek(fp, 0, SEEK_SET);
    fread(hdr, sizeof(FileHeader), 1, fp);
}

static void write_page(FILE *fp, uint32_t page_id, const uint8_t *page) {
    fseek(fp, (long)page_id * PAGE_SIZE, SEEK_SET);
    fwrite(page, PAGE_SIZE, 1, fp);
    fflush(fp);
}

static void read_page(FILE *fp, uint32_t page_id, uint8_t *page) {
    fseek(fp, (long)page_id * PAGE_SIZE, SEEK_SET);
    fread(page, PAGE_SIZE, 1, fp);
}

static void init_empty_page(uint8_t *page, bool is_leaf) {
    memset(page, 0, PAGE_SIZE);
    PageHeader *hdr = (PageHeader *)page;
    hdr->is_leaf = is_leaf ? 1 : 0;
    hdr->num_keys = 0;
    hdr->free_space_start = sizeof(PageHeader);
    hdr->free_space_end = PAGE_SIZE;
    hdr->right_link = NULL_PAGE;
}

static uint32_t allocate_page(FILE *fp, FileHeader *file_hdr, bool is_leaf) {
    uint32_t new_id = file_hdr->next_page_id++;
    uint8_t page[PAGE_SIZE];
    init_empty_page(page, is_leaf);
    write_page(fp, new_id, page);
    write_file_header(fp, file_hdr);
    return new_id;
}

// ============================================================================
// 2. SLOTTED-PAGE HELPERS (static)
// Cell layout: [uint32_t left_child][DiskOffset val][uint16_t key_len][key]
// ============================================================================

static inline uint16_t* get_slots(uint8_t *page) {
    return (uint16_t *)(page + sizeof(PageHeader));
}

static int compare_keys(const char *a, uint16_t alen, const char *b, uint16_t blen) {
    uint16_t min_len = (alen < blen) ? alen : blen;
    int cmp = memcmp(a, b, min_len);
    if (cmp != 0) return cmp;
    return (alen > blen) - (alen < blen);
}

static uint16_t cell_size(uint16_t key_len) {
    return sizeof(uint32_t) + sizeof(DiskOffset) + sizeof(uint16_t) + key_len;
}

static uint16_t page_used_bytes(const uint8_t *page) {
    const PageHeader *hdr = (const PageHeader *)page;
    return (hdr->free_space_start - sizeof(PageHeader)) + (PAGE_SIZE - hdr->free_space_end);
}

static bool page_has_space(const uint8_t *page, uint16_t key_len) {
    const PageHeader *hdr = (const PageHeader *)page;
    uint16_t available = hdr->free_space_end - hdr->free_space_start;
    uint16_t needed = sizeof(uint16_t) + cell_size(key_len);
    return available >= needed;
}

static uint32_t get_left_child(uint8_t *page, int slot_idx) {
    uint16_t offset = get_slots(page)[slot_idx];
    uint32_t child;
    memcpy(&child, page + offset, sizeof(uint32_t));
    return child;
}

static void set_left_child(uint8_t *page, int slot_idx, uint32_t child_id) {
    uint16_t offset = get_slots(page)[slot_idx];
    memcpy(page + offset, &child_id, sizeof(uint32_t));
}

static DiskOffset get_cell_val(uint8_t *page, int slot_idx) {
    uint16_t offset = get_slots(page)[slot_idx];
    DiskOffset val;
    memcpy(&val, page + offset + sizeof(uint32_t), sizeof(DiskOffset));
    return val;
}

static void get_cell_key(uint8_t *page, int slot_idx, const char **key_out, uint16_t *len_out) {
    uint16_t offset = get_slots(page)[slot_idx];
    uint8_t *ptr = page + offset + sizeof(uint32_t) + sizeof(DiskOffset);
    memcpy(len_out, ptr, sizeof(uint16_t));
    *key_out = (const char *)(ptr + sizeof(uint16_t));
}

static uint32_t get_child_at(uint8_t *page, int child_idx) {
    PageHeader *hdr = (PageHeader *)page;
    return (child_idx < hdr->num_keys) ? get_left_child(page, child_idx) : hdr->right_link;
}

static int find_slot(uint8_t *page, const char *key, uint16_t key_len, bool *found) {
    PageHeader *hdr = (PageHeader *)page;
    int low = 0, high = hdr->num_keys - 1;
    *found = false;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        const char *mkey;
        uint16_t mlen;
        get_cell_key(page, mid, &mkey, &mlen);

        int cmp = compare_keys(key, key_len, mkey, mlen);
        if (cmp == 0) {
            *found = true;
            return mid;
        }
        if (cmp < 0) high = mid - 1;
        else         low = mid + 1;
    }
    return low;
}

static void insert_cell(uint8_t *page, int idx, const char *key, uint16_t key_len,
                        DiskOffset val, uint32_t left_child) {
    PageHeader *hdr = (PageHeader *)page;
    uint16_t csize = cell_size(key_len);

    hdr->free_space_end -= csize;
    uint8_t *ptr = page + hdr->free_space_end;
    memcpy(ptr, &left_child, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(ptr, &val, sizeof(DiskOffset));
    ptr += sizeof(DiskOffset);
    memcpy(ptr, &key_len, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, key, key_len);

    uint16_t *slots = get_slots(page);
    for (int i = hdr->num_keys; i > idx; i--) {
        slots[i] = slots[i - 1];
    }
    slots[idx] = hdr->free_space_end;
    hdr->free_space_start += sizeof(uint16_t);
    hdr->num_keys++;
}

static void insert_entry(uint8_t *page, const char *key, uint16_t key_len,
                         DiskOffset val, uint32_t left_id, uint32_t right_id) {
    bool found;
    int idx = find_slot(page, key, key_len, &found);
    insert_cell(page, idx, key, key_len, val, left_id);

    PageHeader *hdr = (PageHeader *)page;
    if (idx + 1 < hdr->num_keys) {
        set_left_child(page, idx + 1, right_id);
    } else {
        hdr->right_link = right_id;
    }
}

// Removes cell at idx and compacts the slotted page
static void remove_cell(uint8_t *page, int idx) {
    PageHeader *hdr = (PageHeader *)page;
    uint8_t tmp[PAGE_SIZE];
    init_empty_page(tmp, hdr->is_leaf != 0);
    ((PageHeader *)tmp)->right_link = hdr->right_link;

    int dst = 0;
    for (int i = 0; i < hdr->num_keys; i++) {
        if (i == idx) continue;
        const char *k;
        uint16_t klen;
        get_cell_key(page, i, &k, &klen);
        insert_cell(tmp, dst++, k, klen, get_cell_val(page, i), get_left_child(page, i));
    }
    memcpy(page, tmp, PAGE_SIZE);
}

// Replaces key+val at idx (preserving left_child) and compacts the page
static void replace_cell(uint8_t *page, int idx, const char *new_key, uint16_t new_len, DiskOffset new_val) {
    PageHeader *hdr = (PageHeader *)page;
    uint8_t tmp[PAGE_SIZE];
    init_empty_page(tmp, hdr->is_leaf != 0);
    ((PageHeader *)tmp)->right_link = hdr->right_link;

    for (int i = 0; i < hdr->num_keys; i++) {
        uint32_t lc = get_left_child(page, i);
        if (i == idx) {
            insert_cell(tmp, i, new_key, new_len, new_val, lc);
        } else {
            const char *k;
            uint16_t klen;
            get_cell_key(page, i, &k, &klen);
            insert_cell(tmp, i, k, klen, get_cell_val(page, i), lc);
        }
    }
    memcpy(page, tmp, PAGE_SIZE);
}

// ============================================================================
// 3. CORE B-TREE API (OPEN, CLOSE, SEARCH, INSERT, DELETE)
// ============================================================================

FILE* btree_open(const char *filename, FileHeader *file_hdr) {
    FILE *fp = fopen(filename, "r+b");
    if (fp) {
        read_file_header(fp, file_hdr);
        return fp;
    }
    fp = fopen(filename, "w+b");
    if (!fp) return NULL;

    file_hdr->root_page_id = 1;
    file_hdr->next_page_id = 1;
    allocate_page(fp, file_hdr, true);
    return fp;
}

void btree_close(FILE *fp, const FileHeader *file_hdr) {
    if (!fp) return;
    if (file_hdr) {
        write_file_header(fp, file_hdr);
    }
    fclose(fp);
}

bool btree_search(FILE *fp, const FileHeader *file_hdr, const char *key, DiskOffset *result) {
    uint16_t key_len = (uint16_t)strlen(key);
    uint32_t curr_id = file_hdr->root_page_id;
    uint8_t page[PAGE_SIZE];

    while (curr_id != NULL_PAGE) {
        read_page(fp, curr_id, page);
        PageHeader *hdr = (PageHeader *)page;

        bool found = false;
        int idx = find_slot(page, key, key_len, &found);
        if (found) {
            *result = get_cell_val(page, idx);
            return true;
        }
        if (hdr->is_leaf) return false;
        curr_id = get_child_at(page, idx);
    }
    return false;
}

static uint32_t split_page(FILE *fp, FileHeader *file_hdr, uint32_t left_id, uint8_t *old_page,
                           char *ins_key, uint16_t *ins_len, DiskOffset *ins_val,
                           uint32_t ins_left_id, uint32_t ins_right_id) {
    PageHeader *old_hdr = (PageHeader *)old_page;
    bool is_leaf = (old_hdr->is_leaf != 0);
    int total = old_hdr->num_keys + 1;
    TempCell *cells = (TempCell *)malloc(sizeof(TempCell) * total);

    int c = 0, ins_pos = -1;
    uint32_t total_bytes = 0;
    uint32_t updated_rightmost = old_hdr->right_link;

    for (int i = 0; i < old_hdr->num_keys; i++) {
        const char *k;
        uint16_t klen;
        get_cell_key(old_page, i, &k, &klen);

        if (ins_pos == -1 && compare_keys(ins_key, *ins_len, k, klen) < 0) {
            memcpy(cells[c].key, ins_key, *ins_len);
            cells[c].key_len = *ins_len;
            cells[c].val = *ins_val;
            cells[c].left_child = ins_left_id;
            total_bytes += sizeof(uint16_t) + cell_size(*ins_len);
            ins_pos = c++;
        }
        memcpy(cells[c].key, k, klen);
        cells[c].key_len = klen;
        cells[c].val = get_cell_val(old_page, i);
        cells[c].left_child = get_left_child(old_page, i);
        total_bytes += sizeof(uint16_t) + cell_size(klen);
        c++;
    }
    if (ins_pos == -1) {
        memcpy(cells[c].key, ins_key, *ins_len);
        cells[c].key_len = *ins_len;
        cells[c].val = *ins_val;
        cells[c].left_child = ins_left_id;
        total_bytes += sizeof(uint16_t) + cell_size(*ins_len);
        ins_pos = c;
    }

    if (ins_pos + 1 < total) {
        cells[ins_pos + 1].left_child = ins_right_id;
    } else {
        updated_rightmost = ins_right_id;
    }

    uint32_t half_bytes = total_bytes / 2;
    uint32_t bytes_so_far = 0;
    int split_idx = 0;
    while (bytes_so_far < half_bytes && split_idx < total - 2) {
        bytes_so_far += sizeof(uint16_t) + cell_size(cells[split_idx].key_len);
        split_idx++;
    }
    if (split_idx == 0) split_idx = 1;

    uint32_t right_id = allocate_page(fp, file_hdr, is_leaf);
    uint8_t left_page[PAGE_SIZE], right_page[PAGE_SIZE];
    init_empty_page(left_page, is_leaf);
    init_empty_page(right_page, is_leaf);

    for (int i = 0; i < split_idx; i++) {
        insert_cell(left_page, i, cells[i].key, cells[i].key_len, cells[i].val, cells[i].left_child);
    }
    ((PageHeader *)left_page)->right_link = cells[split_idx].left_child;

    for (int i = split_idx + 1; i < total; i++) {
        insert_cell(right_page, i - (split_idx + 1), cells[i].key, cells[i].key_len, cells[i].val, cells[i].left_child);
    }
    ((PageHeader *)right_page)->right_link = updated_rightmost;

    memcpy(ins_key, cells[split_idx].key, cells[split_idx].key_len);
    *ins_len = cells[split_idx].key_len;
    *ins_val = cells[split_idx].val;

    write_page(fp, left_id, left_page);
    write_page(fp, right_id, right_page);
    free(cells);
    return right_id;
}

bool btree_insert(FILE *fp, FileHeader *file_hdr, const char *key, DiskOffset val) {
    uint16_t key_len = (uint16_t)strlen(key);
    if (key_len == 0 || key_len > MAX_INLINE_KEY) return false;

    uint32_t path[MAX_TREE_DEPTH];
    int depth = 0;
    uint32_t curr_id = file_hdr->root_page_id;
    uint8_t page[PAGE_SIZE];

    while (true) {
        read_page(fp, curr_id, page);
        PageHeader *hdr = (PageHeader *)page;

        bool found = false;
        int idx = find_slot(page, key, key_len, &found);
        if (found) return false;

        if (hdr->is_leaf) break;
        path[depth++] = curr_id;
        curr_id = get_child_at(page, idx);
    }

    char ins_key[MAX_INLINE_KEY];
    memcpy(ins_key, key, key_len);
    uint16_t ins_len = key_len;
    DiskOffset ins_val = val;
    uint32_t left_id = NULL_PAGE;
    uint32_t right_id = NULL_PAGE;

    while (true) {
        if (page_has_space(page, ins_len)) {
            insert_entry(page, ins_key, ins_len, ins_val, left_id, right_id);
            write_page(fp, curr_id, page);
            return true;
        }

        right_id = split_page(fp, file_hdr, curr_id, page,
                              ins_key, &ins_len, &ins_val,
                              left_id, right_id);
        left_id = curr_id;

        if (depth == 0) {
            uint32_t new_root_id = allocate_page(fp, file_hdr, false);
            uint8_t new_root[PAGE_SIZE];
            init_empty_page(new_root, false);
            insert_cell(new_root, 0, ins_key, ins_len, ins_val, left_id);
            ((PageHeader *)new_root)->right_link = right_id;
            write_page(fp, new_root_id, new_root);

            file_hdr->root_page_id = new_root_id;
            write_file_header(fp, file_hdr);
            return true;
        }

        curr_id = path[--depth];
        read_page(fp, curr_id, page);
    }
}

bool btree_delete(FILE *fp, FileHeader *file_hdr, const char *key) {
    uint16_t key_len = (uint16_t)strlen(key);
    uint32_t path_page[MAX_TREE_DEPTH];
    int      path_idx[MAX_TREE_DEPTH];
    int      depth = 0;

    uint32_t curr_id = file_hdr->root_page_id;
    uint8_t  page[PAGE_SIZE];
    int      found_idx = -1;

    // 1. Locate the key while recording the descent path
    while (curr_id != NULL_PAGE) {
        read_page(fp, curr_id, page);
        PageHeader *hdr = (PageHeader *)page;

        bool found = false;
        int idx = find_slot(page, key, key_len, &found);
        if (found) {
            found_idx = idx;
            break;
        }
        if (hdr->is_leaf) return false;

        path_page[depth] = curr_id;
        path_idx[depth]  = idx;
        depth++;
        curr_id = get_child_at(page, idx);
    }
    if (found_idx == -1) return false;

    // 2. If in an internal node, replace with in-order predecessor from leaf
    if (!((PageHeader *)page)->is_leaf) {
        uint32_t target_id = curr_id;
        uint8_t  target_page[PAGE_SIZE];
        memcpy(target_page, page, PAGE_SIZE);

        path_page[depth] = target_id;
        path_idx[depth]  = found_idx;
        depth++;
        curr_id = get_left_child(target_page, found_idx);

        while (true) {
            read_page(fp, curr_id, page);
            PageHeader *hdr = (PageHeader *)page;
            if (hdr->is_leaf) break;

            path_page[depth] = curr_id;
            path_idx[depth]  = hdr->num_keys; // Follow rightmost link
            depth++;
            curr_id = hdr->right_link;
        }

        int pred_idx = ((PageHeader *)page)->num_keys - 1;
        const char *pk;
        uint16_t plen;
        get_cell_key(page, pred_idx, &pk, &plen);
        char pred_key[MAX_INLINE_KEY];
        memcpy(pred_key, pk, plen);
        DiskOffset pred_val = get_cell_val(page, pred_idx);

        replace_cell(target_page, found_idx, pred_key, plen, pred_val);
        write_page(fp, target_id, target_page);

        remove_cell(page, pred_idx);
        write_page(fp, curr_id, page);
    } else {
        remove_cell(page, found_idx);
        write_page(fp, curr_id, page);
    }

    // 3. Rebalance bottom-up (borrow from sibling or merge)
    while (depth > 0) {
        PageHeader *hdr = (PageHeader *)page;
        if (hdr->num_keys > 0 && page_used_bytes(page) >= UNDERFLOW_BYTES) {
            return true;
        }

        uint32_t parent_id = path_page[--depth];
        int      child_idx = path_idx[depth];
        uint8_t  parent[PAGE_SIZE];
        read_page(fp, parent_id, parent);
        PageHeader *phdr = (PageHeader *)parent;

        // Try borrowing from LEFT sibling
        if (child_idx > 0) {
            uint32_t left_id = get_child_at(parent, child_idx - 1);
            uint8_t  left_sib[PAGE_SIZE];
            read_page(fp, left_id, left_sib);
            PageHeader *lhdr = (PageHeader *)left_sib;

            const char *sk; uint16_t slen;
            get_cell_key(parent, child_idx - 1, &sk, &slen);

            if (lhdr->num_keys > 1 && page_has_space(page, slen)) {
                char sep_key[MAX_INLINE_KEY];
                memcpy(sep_key, sk, slen);
                DiskOffset sep_val = get_cell_val(parent, child_idx - 1);

                int last = lhdr->num_keys - 1;
                const char *lk; uint16_t llen;
                get_cell_key(left_sib, last, &lk, &llen);
                char move_key[MAX_INLINE_KEY];
                memcpy(move_key, lk, llen);
                DiskOffset move_val = get_cell_val(left_sib, last);
                uint32_t   move_lc  = get_left_child(left_sib, last);

                insert_cell(page, 0, sep_key, slen, sep_val, lhdr->right_link);
                replace_cell(parent, child_idx - 1, move_key, llen, move_val);
                lhdr->right_link = move_lc;
                remove_cell(left_sib, last);

                write_page(fp, left_id, left_sib);
                write_page(fp, curr_id, page);
                write_page(fp, parent_id, parent);
                return true;
            }
        }

        // Try borrowing from RIGHT sibling
        if (child_idx < phdr->num_keys) {
            uint32_t right_id = get_child_at(parent, child_idx + 1);
            uint8_t  right_sib[PAGE_SIZE];
            read_page(fp, right_id, right_sib);
            PageHeader *rhdr = (PageHeader *)right_sib;

            const char *sk; uint16_t slen;
            get_cell_key(parent, child_idx, &sk, &slen);

            if (rhdr->num_keys > 1 && page_has_space(page, slen)) {
                char sep_key[MAX_INLINE_KEY];
                memcpy(sep_key, sk, slen);
                DiskOffset sep_val = get_cell_val(parent, child_idx);

                const char *rk; uint16_t rlen;
                get_cell_key(right_sib, 0, &rk, &rlen);
                char move_key[MAX_INLINE_KEY];
                memcpy(move_key, rk, rlen);
                DiskOffset move_val = get_cell_val(right_sib, 0);
                uint32_t   move_lc  = get_left_child(right_sib, 0);

                insert_cell(page, hdr->num_keys, sep_key, slen, sep_val, hdr->right_link);
                hdr->right_link = move_lc;
                replace_cell(parent, child_idx, move_key, rlen, move_val);
                remove_cell(right_sib, 0);

                write_page(fp, curr_id, page);
                write_page(fp, right_id, right_sib);
                write_page(fp, parent_id, parent);
                return true;
            }
        }

        // Try merging with LEFT sibling
        bool merged = false;
        if (child_idx > 0) {
            uint32_t left_id = get_child_at(parent, child_idx - 1);
            uint8_t  left_sib[PAGE_SIZE];
            read_page(fp, left_id, left_sib);
            PageHeader *lhdr = (PageHeader *)left_sib;

            const char *sk; uint16_t slen;
            get_cell_key(parent, child_idx - 1, &sk, &slen);
            uint32_t needed = page_used_bytes(left_sib) + page_used_bytes(page) + sizeof(uint16_t) + cell_size(slen);

            if (needed <= PAGE_SIZE - sizeof(PageHeader)) {
                char sep_key[MAX_INLINE_KEY];
                memcpy(sep_key, sk, slen);
                DiskOffset sep_val = get_cell_val(parent, child_idx - 1);

                insert_cell(left_sib, lhdr->num_keys, sep_key, slen, sep_val, lhdr->right_link);
                for (int i = 0; i < hdr->num_keys; i++) {
                    const char *ck; uint16_t clen;
                    get_cell_key(page, i, &ck, &clen);
                    insert_cell(left_sib, lhdr->num_keys, ck, clen, get_cell_val(page, i), get_left_child(page, i));
                }
                lhdr->right_link = hdr->right_link;

                remove_cell(parent, child_idx - 1);
                if (child_idx - 1 < phdr->num_keys) {
                    set_left_child(parent, child_idx - 1, left_id);
                } else {
                    phdr->right_link = left_id;
                }

                write_page(fp, left_id, left_sib);
                write_page(fp, parent_id, parent);
                merged = true;
            }
        }

        // Try merging with RIGHT sibling
        if (!merged && child_idx < phdr->num_keys) {
            uint32_t right_id = get_child_at(parent, child_idx + 1);
            uint8_t  right_sib[PAGE_SIZE];
            read_page(fp, right_id, right_sib);
            PageHeader *rhdr = (PageHeader *)right_sib;

            const char *sk; uint16_t slen;
            get_cell_key(parent, child_idx, &sk, &slen);
            uint32_t needed = page_used_bytes(page) + page_used_bytes(right_sib) + sizeof(uint16_t) + cell_size(slen);

            if (needed <= PAGE_SIZE - sizeof(PageHeader)) {
                char sep_key[MAX_INLINE_KEY];
                memcpy(sep_key, sk, slen);
                DiskOffset sep_val = get_cell_val(parent, child_idx);

                insert_cell(page, hdr->num_keys, sep_key, slen, sep_val, hdr->right_link);
                for (int i = 0; i < rhdr->num_keys; i++) {
                    const char *rk; uint16_t rlen;
                    get_cell_key(right_sib, i, &rk, &rlen);
                    insert_cell(page, hdr->num_keys, rk, rlen, get_cell_val(right_sib, i), get_left_child(right_sib, i));
                }
                hdr->right_link = rhdr->right_link;

                remove_cell(parent, child_idx);
                if (child_idx < phdr->num_keys) {
                    set_left_child(parent, child_idx, curr_id);
                } else {
                    phdr->right_link = curr_id;
                }

                write_page(fp, curr_id, page);
                write_page(fp, parent_id, parent);
                merged = true;
            }
        }

        if (!merged) return true;
        curr_id = parent_id;
        memcpy(page, parent, PAGE_SIZE);
    }

    // 4. Shrink root if it became empty
    PageHeader *root_hdr = (PageHeader *)page;
    if (!root_hdr->is_leaf && root_hdr->num_keys == 0) {
        file_hdr->root_page_id = root_hdr->right_link;
        write_file_header(fp, file_hdr);
    }
    return true;
}

// ============================================================================
// 4. TEST SUITE
// ============================================================================

static void make_key(int i, char *buf, size_t sz) {
    if (i % 3 == 0)      snprintf(buf, sz, "k_%04d", i);
    else if (i % 3 == 1) snprintf(buf, sz, "user_email_address_number_%04d@example.com", i);
    else                 snprintf(buf, sz, "long_variable_length_record_key_payload_identifier_seq_%04d_end", i);
}