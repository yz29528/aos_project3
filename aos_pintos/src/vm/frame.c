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
static unsigned frame_hash(const struct hash_elem *e, void* aux UNUSED);
static bool frame_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
struct frame_table_entry* frame_create_frame_table_entry(void* upage,void* frame);

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

    block_sector_t index = swap_store(entry->frame);
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