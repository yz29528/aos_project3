//
// Created by Administrator on 2024/4/4.
//

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "devices/block.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
enum page_status {
    FRAME,
    SWAP
};

struct page_table_entry {
    void *key;
    uint32_t val;
    enum page_status status;
    bool writable;
    struct hash_elem he;
};

void page_init();
/* basic life cycle *
 */
struct hash *page_create_table();
struct page_table_entry* page_find(struct hash *page_table, void *upage);

void page_destroy(struct hash *page_table);
bool page_page_fault_handler(const void *vaddr, bool to_write, void *esp);


#endif