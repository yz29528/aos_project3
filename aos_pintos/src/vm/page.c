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

bool create_framepage (struct hash *page_table, void *upage, void *kpage) {
  struct page_table_entry *entry = (struct page_table_entry *) malloc(sizeof(struct page_table_entry));

  entry->key = upage;
  entry->kpage = kpage;
  entry->status = FRAME;
  entry->dirty = false;
  entry->swap_index = -1;

  struct hash_elem *prev_elem;
  prev_elem = hash_insert(page_table, &entry->he);
  if (prev_elem == NULL) {
    // Successfully inserted into the supplemental page table.
    return true;
  } else {
    // There is already an entry.
    free (entry);
    return false;
  }
}

bool create_zeropage (struct hash *page_table, void *upage) {
  struct page_table_entry *spte = (struct page_table_entry *) malloc(sizeof(struct page_table_entry));

  spte->key = upage;
  spte->kpage = NULL;
  spte->status = ALL_ZERO;
  spte->dirty = false;

  struct hash_elem *prev_elem;
  prev_elem = hash_insert(page_table, &spte->he);
  if (prev_elem == NULL) {
    return true;
  }

  // there is already an entry -- impossible state
  PANIC("Duplicated page table entry for zeropage");
  return false;
}

/* Second, the kernel consults the supplemental page table
 when a process terminates, to decide what resources to free. */
void page_table_destructor(struct hash_elem *e, void *aux UNUSED) {
    struct page_table_entry *entry = hash_entry(e, struct page_table_entry, he);
    if (entry->status == SWAP) {
        uint32_t index=entry->val;
        if (index != -1) {
            swap_free(index);
        }
    } else if (entry->status == FRAME) {
        frame_remove_entry(entry->kpage);
    }
    free(entry);
}

bool set_swap(struct hash *page_table, void *page, block_sector_t swap_index) {
  struct page_table_entry *entry = page_find(page_table, page);
  if (entry == NULL) {
    return false;
  }

  entry->status = SWAP;
  entry->kpage = NULL;
  entry->swap_index = swap_index;
  return true;
}

bool page_table_has_entry (struct hash *page_table, void *page) {
  struct page_table_entry *entry = page_find(page_table, page);

  return entry != NULL;
}

bool set_dirty (struct hash *page_table, void *page, bool value) {
  struct page_table_entry *entry = page_find(page_table, page);
  if (entry == NULL) {
    PANIC("set dirty - the request page doesn't exist");
  }

  entry->dirty = entry->dirty || value;
  return true;
}

/* Use a heuristic to check for stack access. We check if the
   address is in the user space and the fault access is at 
   most 32 bytes below the stack pointer. */
bool stack_access(const void *esp, void *addr){
  return (uint32_t)addr > 0 && addr >= (esp - 32) && (PHYS_BASE - pg_round_down (addr)) <= (1 << 23);
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
    lock_acquire(&page_table_lock);
    hash_destroy(page_table, page_table_destructor);
    free(page_table);
    lock_release(&page_table_lock);
}

bool page_table_load_page(struct hash *page_table, uint32_t *pagedir, void *upage) {
  // Check if the memory reference is valid
  struct page_table_entry *entry = page_find(page_table, upage);
  if (entry == NULL) {
    return false;
  }

  // Check if we've already loaded the page
  if (entry->status == FRAME) {
    return true;
  }

  // Obtain a frame to store the page
  void *frame_page = frame_allocate(PAL_USER, upage);
  if (frame_page == NULL) {
    return false;
  }

  // Fetch the data into the frame
  bool writable = true;
  switch (entry->status) {
    case ALL_ZERO:
        memset (frame_page, 0, PGSIZE);
        break;
    case FRAME:
        // Do Nothing
        break;
    case SWAP:
        // Swap in: load the data from the swap disc
        swap_in(entry->swap_index, frame_page);
        break;
    default:
        PANIC("Unreachable state");
  }

  // Point the page table entry for the faulting virtual address to the physical page.
  if (!pagedir_set_page(pagedir, upage, frame_page, writable)) {
    frame_free(frame_page);
    return false;
  }

  // Store frame page within the entry
  entry->kpage = frame_page;
  entry->status = FRAME;

  pagedir_set_dirty(pagedir, frame_page, false);

  frame_unpin(frame_page);

  return true;
}

void pin_page(struct hash *page_table, void *page) {
  struct page_table_entry *entry = page_find(page_table, page);
  if(entry == NULL) {
    // ignore. stack may be grow
    return;
  }

  ASSERT(entry->status == FRAME);
  frame_pin(entry->kpage);
}

void unpin_page(struct hash *page_table, void *page) {
  struct page_table_entry *entry = page_find(page_table, page);
  if(entry == NULL) {
    PANIC ("request page is non-existent");
  } 

  if (entry->status == FRAME) {
    frame_unpin(entry->kpage);
  }
}

/* Verify that there's not already a page at that virtual
 address, then map our page there. */
bool page_set_frame(void *upage, void *kpage, bool writable) {
    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    uint32_t *pagedir = cur->pagedir;
    ASSERT(kpage!=NULL)
    lock_acquire(&page_table_lock);
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
        lock_release(&page_table_lock);
        return true;
    }
    lock_release(&page_table_lock);
    return false;
}


// todo
bool page_fault_handler(const void *vaddr, bool writable, void *esp) {

    struct thread *cur = thread_current();
    struct hash* page_table = cur->page_table;
    uint32_t *pagedir = cur->pagedir;
    void *upage = pg_round_down(vaddr);

    bool success = false;
    lock_acquire(&page_table_lock);

    struct page_table_entry* entry = page_find(page_table, upage);


    void *kpage = NULL;

    if(upage >= (void*)PAGE_STACK_UNDERLINE) {
        if(vaddr >= (void*)((unsigned int)(esp) - POINTER_SIZE)) {
            if(entry == NULL) {
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
            }else if(entry->status==SWAP) {
                kpage = frame_get_fr(PAL_DEFAULT, upage);
                if(kpage != NULL) {
                    swap_in( entry->val, kpage);
                    entry->val =(uint32_t) kpage;
                    entry->status = FRAME;
                    success=true;
                }
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


