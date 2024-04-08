//
// Created by Administrator on 2024/4/4.
//

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "devices/block.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
#include "threads/palloc.h"
#include "vm/swap.h"

enum page_status {
    ALL_ZERO,
    FRAME,
    SWAP
};

struct page_table_entry {
    void *key; // upage
    void *kpage; // kernel page
    uint32_t val;
    enum page_status status;
    bool writable;
    struct hash_elem he;
    bool dirty;
    block_sector_t swap_index;
};

void page_init();
/* basic life cycle *
 */
struct hash *page_create_table();
struct page_table_entry* page_find(struct hash *page_table, void *upage);
bool page_evict_upage(struct thread *holder, void *upage, uint32_t index);
void page_destroy_table(struct hash *page_table);
bool page_fault_handler(const void *vaddr, bool to_write, void *esp);
bool page_set_frame(void *upage, void *kpage, bool writable);

bool stack_access(const void *esp, void *addr);

#endif