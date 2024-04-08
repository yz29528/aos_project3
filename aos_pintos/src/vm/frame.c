#include <stdio.h>
#include "page.h"
#include "frame.h"
#include "swap.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "lib/string.h"
#include "lib/stddef.h"
#include "threads/vaddr.h"
static struct hash frame_table;
static struct list frame_list;
static struct lock frame_table_lock;
static struct list_elem *clock_ptr = NULL;

static unsigned frame_hash(const struct hash_elem *e, void* aux UNUSED);
static bool frame_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
struct frame_table_entry* frame_create_frame_table_entry(void* upage,void* frame);
struct frame_table_entry* pick_frame_to_evict(uint32_t *pagedir);

static bool frame_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
    struct frame_table_entry* fa = hash_entry(a,  struct frame_table_entry, he);
    struct frame_table_entry* fb = hash_entry(b,  struct frame_table_entry, he);
    return fa->frame < fb->frame;
}

static unsigned frame_hash(const struct hash_elem *e, void* aux UNUSED){
    struct frame_table_entry* f= hash_entry(e, struct frame_table_entry, he);
    return hash_bytes(&f->frame, sizeof(f->frame));
}

void frame_init() {
    hash_init(&frame_table, frame_hash, frame_hash_less, NULL);
    list_init(&frame_list);
    lock_init(&frame_table_lock);
}

struct frame_table_entry* frame_create_frame_table_entry(void* upage,void* frame){
    struct frame_table_entry* entry= (struct frame_table_entry*)malloc(sizeof (struct frame_table_entry));
    entry->frame = frame;
    entry->upage = upage;
    entry->holder = thread_current();
    return entry;
}

struct frame_table_entry* frame_find_entry(void *frame) {
    struct frame_table_entry temp_entry;
    temp_entry.frame=frame;
    struct hash_elem* e= hash_find(&frame_table,&(temp_entry.he));
    return e!=NULL?hash_entry(e,struct frame_table_entry,he):NULL;
}

// call it in timer to approximate LRU.
void frame_lift_fr(bool execute) {
    if(!execute){
        return;
    }
    struct frame_table_entry *entry;
    for (struct list_elem* e = list_rbegin(&frame_list); e != list_rend(&frame_list); e = list_prev(e)){
        entry= list_entry(e, struct frame_table_entry, le);
        if(pagedir_is_accessed(entry->holder->pagedir, entry->upage)){
            pagedir_set_accessed(entry->holder->pagedir, entry->upage, false);
            list_remove(&entry->le);
            list_push_front(&frame_list,&entry->le);
            break;
        }
    }
}

struct frame_table_entry* frame_get_used_fr(void *upage) {

    struct list_elem* e =list_pop_back(&frame_list);
    ASSERT(e!=NULL);
    struct frame_table_entry *entry = list_entry(e, struct frame_table_entry, le);

    block_sector_t index = swap_out(entry->frame);
        if (index == (block_sector_t)-1) {
            return NULL;
        }
    ASSERT(page_evict_upage(entry->holder, entry->upage, index));
    entry->upage=upage;
    entry->holder=thread_current();
    list_remove(e);
    list_push_front(&frame_list,e);
    return entry;
}
//get a frame from user pool, which must be mapped from upage
//in other words, in page_table, upage->frame_get_frame(flag, upage)
//flag is used by palloc_get_page
// frame is a b kernel virtual address rather than physic address
void* frame_get_fr(enum palloc_flags flag, void *upage) {

    ASSERT (pg_ofs (upage) == 0);
    ASSERT (is_user_vaddr (upage));

    lock_acquire(&frame_table_lock);
    struct frame_table_entry *entry;
    void *frame = palloc_get_page(PAL_USER | flag);
    if (frame != NULL){
        ASSERT(pg_ofs(frame) == 0);
        if (flag == PAL_ZERO){
            memset (frame, 0, PGSIZE);
        }
        entry=frame_create_frame_table_entry(upage,frame);
        ASSERT(entry!=NULL && entry->frame!=NULL);
       //printf("thread %s insert a entry usage: %x  frame:%x\n",thread_current()->name,upage,frame);
        list_push_front(&frame_list,&entry->le);
        hash_insert(&frame_table, &entry->he);
        lock_release(&frame_table_lock);
        //printf("get a frame from palloc:%x\n",frame);
        return frame;
    }
    //PANIC("run out of user pool and !");
    entry=frame_get_used_fr(upage);
        if (entry != NULL){
            list_remove(&entry->le);
            list_push_front(&frame_list,&entry->le);
        }
       //
    lock_release(&frame_table_lock);
    return entry->frame;
}

void* frame_allocate(enum palloc_flags flags, void *upage) {
  lock_acquire (&frame_table_lock);

  void *frame_page = palloc_get_page(PAL_USER | flags);
  if (frame_page == NULL) {
    // Page allocation failed

    /* first, swap out the page */
    struct frame_table_entry *f_evicted = pick_frame_to_evict(thread_current()->pagedir);
    ASSERT(f_evicted != NULL && f_evicted->holder != NULL);

    // clear the page mapping, and replace it with swap
    ASSERT(f_evicted->holder->pagedir != (void*)0xcccccccc);
    pagedir_clear_page(f_evicted->holder->pagedir, f_evicted->upage);

    bool is_dirty = false;
    is_dirty = is_dirty || pagedir_is_dirty(f_evicted->holder->pagedir, f_evicted->upage);
    is_dirty = is_dirty || pagedir_is_dirty(f_evicted->holder->pagedir, f_evicted->kpage);
    
    block_sector_t swap_idx = swap_out(f_evicted->kpage);
    set_swap(f_evicted->holder->page_table, f_evicted->upage, swap_idx);
    set_dirty(f_evicted->holder->page_table, f_evicted->upage, is_dirty);
    frame_do_free(f_evicted->kpage, true); // f_evicted is also invalidated

    frame_page = palloc_get_page (PAL_USER | flags);
    ASSERT (frame_page != NULL); // should success in this chance
  }

  struct frame_table_entry *frame = malloc(sizeof(struct frame_table_entry));
  if (frame == NULL) {
    // Frame allocation failed
    lock_release(&frame_table_lock);
    return NULL;
  }

  frame->holder = thread_current();
  frame->upage = upage;
  frame->kpage = frame_page;
  frame->pinned = true;         // can't be evicted yet

  hash_insert(&frame_table, &frame->he);
  list_push_back(&frame_list, &frame->le);

  lock_release(&frame_table_lock);
  return frame_page;
}

/** Use The Clock Algorithm as a frame eviction strategy*/
struct frame_table_entry* clock_frame_next(void);
struct frame_table_entry* pick_frame_to_evict(uint32_t *pagedir) {
  size_t n = hash_size(&frame_table);
  if (n == 0) {
    PANIC("Frame table is empty, can't happen - there is a leak somewhere");
  }

  for (int it = 0; it <= 2 * n; it++) { // Prevent infinite loop
    struct frame_table_entry *e = clock_frame_next();

    if (e->pinned) {
        continue;
    } else if (pagedir_is_accessed(pagedir, e->upage)) {
      pagedir_set_accessed(pagedir, e->upage, false);
      continue;
    }

    // OK, here is the victim : unreferenced since its last chance
    return e;
  }

  PANIC ("Can't evict any frame -- Not enough memory!\n");
}

struct frame_table_entry* clock_frame_next(void) {
  if (list_empty(&frame_list)) {
    PANIC("Frame table is empty, can't happen - there is a leak somewhere");
  }

  if (clock_ptr == NULL || clock_ptr == list_end(&frame_list)) {
    clock_ptr = list_begin(&frame_list);
  } else {
    clock_ptr = list_next(clock_ptr);
  }

  return list_entry(clock_ptr, struct frame_table_entry, le);
}

void frame_do_free(void *kpage, bool free_page) {
  ASSERT(lock_held_by_current_thread(&frame_table_lock) == true);
  ASSERT(is_kernel_vaddr(kpage));
  ASSERT(pg_ofs (kpage) == 0); // should be aligned

  struct frame_table_entry f_tmp;
  f_tmp.kpage = kpage;

  struct hash_elem *h = hash_find(&frame_table, &(f_tmp.he));
  if (h == NULL) {
    PANIC("The page to be freed is not stored in the table");
  }

  struct frame_table_entry *f = hash_entry(h, struct frame_table_entry, he);

  hash_delete(&frame_table, &f->he);
  list_remove(&f->le);

  // Free resources
  if (free_page) {
    palloc_free_page(kpage);
  }
  free(f);
}

void frame_free(void *kpage) {
  lock_acquire(&frame_table_lock);
  frame_do_free(kpage, true);
  lock_release(&frame_table_lock);
}

void frame_remove_entry(void *kpage) {
  lock_acquire (&frame_table_lock);
  frame_do_free (kpage, false);
  lock_release (&frame_table_lock);
}

//free a frame that got from frame_get_frame
void frame_free_fr(void *frame) {
    ASSERT (pg_ofs (frame) == 0);
    lock_acquire(&frame_table_lock);
    struct frame_table_entry *entry=frame_find_entry(frame);
    //ASSERT(entry->frame!=NULL);// IMPORTANT
    //printf("thread %s try to delete a frame:%x\n",thread_current()->name,frame);
    if (entry != NULL) {
        if (entry->frame == NULL)
            PANIC("try_free_a frame_that_not_exist!!");
        hash_delete(&frame_table, &entry->he);
        list_remove(&entry->le);
        palloc_free_page(frame);
        free(entry);
    }
    lock_release(&frame_table_lock);
}

void frame_set_pinned(void *kpage, bool new_value) {
  lock_acquire(&frame_table_lock);

  struct frame_table_entry f_tmp;
  f_tmp.kpage = kpage;

  struct hash_elem *h = hash_find(&frame_table, &(f_tmp.he));
  if (h == NULL) {
    PANIC("The frame to be pinned/unpinned does not exist");
  }

  struct frame_table_entry *f = hash_entry(h, struct frame_table_entry, he);
  f->pinned = new_value;

  lock_release(&frame_table_lock);
}

void frame_unpin(void* kpage) {
  frame_set_pinned(kpage, false);
}

void frame_pin(void* kpage) {
  frame_set_pinned(kpage, true);
}