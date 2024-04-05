#include <stdio.h>
#include "page.h"
#include "frame.h"
#include "swap.h"
#include "threads/synch.h"
#define PAGE_PAL_FLAG			0
#define POINTER_SIZE		32
#define PAGE_STACK_SIZE			0x800000
#define PAGE_STACK_UNDERLINE	(PHYS_BASE - PAGE_STACK_SIZE)

static struct lock page_table_lock;
bool page_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
unsigned page_hash(const struct hash_elem *e, void* aux UNUSED);
struct page_table_entry* page_find(struct hash *page_table, void *upage);

bool page_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    return hash_entry(a, struct page_table_entry, he)->key < hash_entry(b, struct page_table_entry, he)->key;
}

/* hash function of element in hash table*/
unsigned page_hash(const struct hash_elem *e, void* aux UNUSED){
    struct page_table_entry *entry = hash_entry(e, struct page_table_entry, he);
    return hash_bytes(&(entry->key), sizeof(entry->key));
}

struct page_table_entry* page_find(struct hash *page_table, void *upage) {
    struct hash_elem *e;
    struct page_table_entry tmp_entry;

    ASSERT(page_table != NULL);
    tmp_entry.upage = upage;
    e = hash_find(&page_table, &(tmp_entry.he));
    return e!=NULL?hash_entry(e,struct page_table_entry,he):NULL;
}

struct hash* page_create_table() {
    struct hash* page_table= (struct hash*)malloc(sizeof(struct hash));
    hash_init(page_table, page_hash, page_hash_less, NULL);
    return page_table;
}


void page_table_destructor(struct hash_elem *e, void *aux UNUSED) {
    struct page_table_entry *entry = hash_entry(e, struct page_table_entry, he);
    if(entry->page_status==FRAME){
        //todo
        frame_free_fr((void*)entry->val);
    }else if(entry->page_status==SWAP){
        uint32_t index=entry->val;
        swap_free_swap_slot(index);
    }
    free(entry);
}






boolean page_evict_upage(struct thread *holder, void *upage, uint32_t index){
    struct hash_elem* e = page_find(holder->page_table, upage);
    ASSERT(e!=NULL);

    struct page_table_entry *entry =  hash_entry(e,struct page_table_entry, he);
    if(entry == NULL || entry->status != FRAME) {
        return false;
    }
    entry->value = index;
    entry->status = SWAP;
    pagedir_clear_page(holder->pagedir, upage);
    return true;
}

// called in thread_exit?
void page_destroy(struct hash* page_table) {
    lock_acquire(&page_table_lock);
    hash_destroy(page_table, page_table_destructor);
    lock_release(&page_table_lock);
}






/* Verify that there's not already a page at that virtual
 address, then map our page there. */
bool page_set_frame(void *upage, void *kpage, bool writable) {
    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    uint32_t *pagedir = cur->pagedir;

    lock_acquire(&page_table_lock);
    struct page_table_entry* entry = page_find(&page_table, upage);
    if(entry == NULL) {
        entry = malloc(sizeof(struct page_table_entry));
        entry->upage = upage;
        entry->val = (uint32_t)kpage;
        entry->status = FRAME;
        entry->writable = writable;
        hash_insert(page_table, &entry->he);

        ASSERT(pagedir_set_page(pagedir, entry->upage, entry->val, entry->writable));
        lock_release(&page_table_lock);
        return true;
    }
    lock_release(&page_table_lock);
    return false;
}


// todo
bool page_fault_handler(const void *vaddr, bool to_write, void *esp) {

    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    uint32_t *pagedir = cur->pagedir;
    void *upage = pg_round_down(vaddr);

    bool success = false;
    lock_acquire(&page_table_lock);

    struct page_table_entry* entry = page_find(page_table, upage);


    void *dest = NULL;

    if(upage >= PAGE_STACK_UNDERLINE) {
        if(vaddr >= (unsigned int)(esp) - POINTER_SIZE) {
            if(entry == NULL) {
                dest = frame_get_frame(PAGE_PAL_FLAG, upage);
                // if get a frame from user pool
                if(dest != NULL) {
                    entry = malloc(sizeof(page_table_entry));
                    entry->upage = upage;
                    entry->val = dest;
                    entry->status = FRAME;
                    entry->writable = true;
                    hash_insert(page_table, &entry->he);
                    success=true;
                }
            }else if(entry->status==SWAP) {
                dest = frame_get_frame(PAGE_PAL_FLAG, upage);
                if(dest != NULL) {
                    swap_load((index_t) t->val, dest);
                    t->val = dest;
                    t->status = FRAME;
                    success=true;
                }
            }
        }
    }else if(entry!=NULL && entry->status!=FRAME){
        //todo grown stack
        dest = frame_get_frame(PAGE_PAL_FLAG, upage);
        if (dest != NULL) {
            if(entry->status == SWAP) {
                swap_load((index_t) t->value, dest);
                entry->value = dest;
                entry->status = FRAME;
                success=true;
            }
        }
    }

    //bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable)
    /* Adds a mapping in page directory PD from user virtual page
   UPAGE to the physical frame identified by kernel virtual
   address KPAGE.*/
    if(success) {
        pagedir_set_page (pagedir, upage, kpage,writable);
    }
    lock_release(&page_table_lock);
    return success;
}


