#include <stdio.h>
#include "page.h"
#include "frame.h"
#include "swap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "lib/stddef.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "lib/kernel/hash.h"
#define PAL_DEFAULT			0
#define POINTER_SIZE		32
//On many GNU/Linux systems, the default limit is 8 MB
#define PAGE_STACK_LIMIT			0x800000
#define PAGE_STACK_UNDERLINE	((uint32_t)PHYS_BASE - (uint32_t) PAGE_STACK_LIMIT)

static struct lock page_table_lock;
bool page_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
unsigned page_hash(const struct hash_elem *e, void* aux UNUSED);
struct page_table_entry* page_find(struct hash *page_table, void *upage);
void page_table_destructor(struct hash_elem *e, void *aux UNUSED);

void page_init() {
    lock_init(&page_table_lock);
}
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
    tmp_entry.key = upage;
    e = hash_find(page_table, &(tmp_entry.he));
    return e!=NULL?hash_entry(e,struct page_table_entry,he):NULL;
}

struct hash* page_create_table() {
    struct hash* page_table= (struct hash*)malloc(sizeof(struct hash));
    hash_init(page_table, page_hash, page_hash_less, NULL);
    return page_table;
}

/* Second, the kernel consults the supplemental page table
 when a process terminates, to decide what resources to free. */

void page_table_destructor(struct hash_elem *e, void *aux UNUSED) {
    struct page_table_entry *entry = hash_entry(e, struct page_table_entry, he);
    if(entry->status==SWAP){
        uint32_t index=entry->val;
        if(index!=-1) {
            swap_free_swap_slot(index);
        }
    }
    else if(entry->status==FRAME){
        pagedir_clear_page(thread_current()->pagedir, entry->key);
        void* kpage=(void*)entry->val;
        if(kpage!=NULL)
            frame_free_fr(kpage);
    }
    free(entry);
}

bool page_install_demand_page(void *upage, uint32_t cur_ofs, uint32_t page_read_bytes, bool writable) {
    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    lock_acquire(&cur->page_table_lock);
    struct page_table_entry* entry = page_find(page_table, upage);
    if(entry == NULL && upage < PAGE_STACK_UNDERLINE) {
        entry = malloc(sizeof(struct page_table_entry));
        entry->key = upage;
        entry->val = cur_ofs;
        entry->page_read_bytes = page_read_bytes;
        entry->status = FILE;
        entry->writable = writable;
        //printf("thread %s try to install_demand_page to page table: offset %x  and upage is:%x\n",cur->name,cur_ofs,upage);
        hash_insert(page_table, &entry->he);
        lock_release(&cur->page_table_lock);
        return true;
    }
    lock_release(&cur->page_table_lock);
    return false;
}





bool page_evict_upage(struct thread *holder, void *upage, uint32_t index){
    struct page_table_entry* entry= page_find(holder->page_table, upage);
    if(entry == NULL || entry->status != FRAME) {
        return false;
    }
    entry->val = index;
    entry->status = SWAP;
    pagedir_clear_page(holder->pagedir, upage);
    return true;
}

// called in thread_exit?
void page_destroy_table(struct hash* page_table) {
    lock_acquire(&thread_current()->page_table_lock);
    hash_destroy(page_table, page_table_destructor);
    lock_release(&thread_current()->page_table_lock);
}






/* Verify that there's not already a page at that virtual
 address, then map our page there. */
bool page_set_frame(void *upage, void *kpage, bool writable) {
    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    uint32_t *pagedir = cur->pagedir;
    ASSERT(kpage!=NULL)
    lock_acquire(&thread_current()->page_table_lock);
    struct page_table_entry* entry = page_find(page_table, upage);
    if(entry == NULL) {
        entry = malloc(sizeof(struct page_table_entry));
        entry->key = upage;
        entry->val = (uint32_t)kpage;
        entry->status = FRAME;
        entry->writable = writable;
        //printf("thread %s try to insert a kpage to page table:%x  and upage is:%x\n",cur->name,kpage,upage);
        hash_insert(page_table, &entry->he);

        ASSERT(pagedir_set_page(pagedir, entry->key, (void*)entry->val, entry->writable));
        lock_release(&thread_current()->page_table_lock);
        return true;
    }
    lock_release(&thread_current()->page_table_lock);
    return false;
}


// todo
bool page_fault_handler(const void *vaddr, bool writable, void *esp) {

    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    uint32_t *pagedir = cur->pagedir;
    void *upage = pg_round_down(vaddr);

    bool success = false;
    lock_acquire(&cur->page_table_lock);

    struct page_table_entry* entry = page_find(page_table, upage);

    if(writable == true && entry != NULL && entry->writable == false) {
        return false;
    }

    void *kpage = NULL;
    if(entry == NULL) {
        if(upage >= (void*)PAGE_STACK_UNDERLINE && vaddr >= (void*)((unsigned int)(esp) - POINTER_SIZE)) {
            kpage = frame_get_fr(PAL_DEFAULT, upage);
            // if get a frame from user pool
            if(kpage != NULL) {
                entry = malloc(sizeof(struct page_table_entry));
                entry->key = upage;
                entry->val = (uint32_t)kpage;
                entry->status = FRAME;
                entry->writable = writable;
                hash_insert(page_table, &entry->he);
                success=true;
            }
        }

    }else if(entry->status==SWAP) {
        kpage = frame_get_fr(PAL_DEFAULT, upage);
        if(kpage != NULL) {
            swap_load( entry->val, kpage);
            entry->val =(uint32_t) kpage;
            entry->status = FRAME;
            success=true;
        }
    }else if (entry->status == FILE) {
        kpage = frame_get_fr(PAL_DEFAULT, upage);
        if (kpage != NULL) {
            int32_t offset = (int32_t) entry->val;
            //printf("demand paging___\n");

            //acquire_filesys_lock();
            acquire_filesys_lock();
            file_read_at(cur->exec_file, kpage, entry->page_read_bytes, offset);
            release_filesys_lock();
            //release_filesys_lock();
            //printf("demand paging____readpage__%x__\n", entry->page_read_bytes);
            if (PGSIZE > entry->page_read_bytes) {
                memset((void *) ((uint32_t) kpage + entry->page_read_bytes), 0,
                       PGSIZE - entry->page_read_bytes);
                //printf("demand paging____setzero____\n");
            }
            //printf("_____demand paging_____upage_%x__\n",pg_round_down(upage));
            entry->val = (uint32_t) kpage;
            entry->status = FRAME;
            //printf("demand paging_kpage:%x___end____\n",kpage);
            success = true;
        }
    }

    //bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable)
    /* Adds a mapping in page directory PD from user virtual page
   UPAGE to the physical frame identified by kernel virtual
   address KPAGE.*/
    if(success) {
        pagedir_set_page (pagedir, upage, kpage,entry->writable);
    }
    lock_release(&cur->page_table_lock);
    return success;
}


