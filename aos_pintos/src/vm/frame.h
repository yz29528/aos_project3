#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "devices/block.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"

struct frame_table_entry{
    void *frame;
    void *upage;
    void *kpage;
    struct thread* holder;
    struct hash_elem he;
    struct list_elem le;
    bool pinned;
};

void *frame_find_fr(void *frame);
void frame_lift_fr(bool execute);
//init frame_table
//used in thread/init.c
void  frame_init();

//get a frame from user pool, which must be mapped from upage
//in other words, in page_table, upage->frame_get_frame(flag, upage)
//flag is used by palloc_get_page1
void* frame_get_fr(enum palloc_flags flag, void *upage);

//free a frame that got from frame_get_frame
void  frame_free_fr(void *frame);

struct frame_table_entry* frame_create_frame_table_entry(void* upage,void* frame);
void frame_unpin(void* kpage);

#endif