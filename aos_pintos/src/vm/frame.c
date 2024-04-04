


static struct hash frame_table;
static struct list frame_list;
static struct list frame_table_lock;
static unsigned frame_hash(const struct hash_elem *e, void* aux UNUSED);
static bool frame_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

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

//todo
void* frame_evict_used_fr(struct frame_table_entry *entry,void* upage){
    entry->upage=upage;
    entry->holder= thread_current();
    list_remove (&frame_list,&entry->le);
    list_push_front(&frame_list,&entry->le);
}


void frame_lift_fr(void *frame) {
    struct frame_table_entry *entry;
    for (struct list_elem* e = list_rbegin(&frame_list); e != list_rend(&frame_list); e = list_prev(e)){
        entry= list_entry(e, struct frame_table_entry, le);
        if(pagedir_is_accessed(entry->holder->pagedir, entry->upage)){
            pagedir_set_accessed(entry->holder->pagedir, entry->upage, false);
            list_remove(&entry->le)
            list_push_front(&frame_list,&entry->le);
        }
    }
}


void* frame_get_used_fr(void *upage) {
    /*
    struct frame_table_entry *entry;
    for (struct list_elem* e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)){
        entry= list_entry(e, struct frame_table_entry, le);
        if(pagedir_is_accessed(entry->holder->pagedir, entry->upage)){
            pagedir_set_accessed(entry->holder->pagedir, entry->upage, false);
        }else{
            return frame_evict_used_fr(entry,upage);
        }
    }
    entry= list_entry(list_front(&frame_list), struct frame_table_entry, le);
     */
    return frame_evict_used_fr(entry,upage);
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
        frame=frame_get_used_fr(upage)
    }
    // PANIC ("run out of user pool!");
    if (frame != NULL){
        ASSERT(pg_ofs(frame) == 0);
        if (flag == PAL_ZERO){
            memset (frame, 0, PGSIZE);
        }
        entry=frame_create_frame_table_entry(upage,frame);
        list_push_front(&frame_list,&entry->le);
        hash_insert(&frame_table, &entry->he);
    }

    lock_release(&frame_table_lock);
    return frame;
}

//free a frame that got from frame_get_frame
void frame_free_fr(void *frame) {
    ASSERT (pg_ofs (frame) == 0);
    lock_acquire(&frame_table_lock);
    struct frame_table_entry *entry=frame_find_entry(frame);
    hash_delete (&frame_table,&entry->he);
    list_remove (&frame_list,&entry->le);
    palloc_free_page(frame);
    free(entry);
    lock_release(&frame_table_lock);
}