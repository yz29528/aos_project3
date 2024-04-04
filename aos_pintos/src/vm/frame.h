

#ifndef VM_FRAME_H
#define VM_FRAME_H

struct frame_table_entry{
    void *frame;
    void *upage;
    struct thread* holder;
    struct hash_elem he;
    struct list_elem le;
};

void *frame_find_fr(void *frame);

//init frame_table
//used in thread/init.c
void  frame_init();

//get a frame from user pool, which must be mapped from upage
//in other words, in page_table, upage->frame_get_frame(flag, upage)
//flag is used by palloc_get_page
void* frame_get_fr(enum palloc_flags flag, void *upage);

//free a frame that got from frame_get_frame
void  frame_free_fr(void *frame);

#endif