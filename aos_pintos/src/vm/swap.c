#include <stdio.h>
#include "page.h"
#include "frame.h"
#include "swap.h"
// Created by zhangyifan on 2024/4/2.
//store the empty swap slot. reuse them later.
static struct list swap_slot_list;
struct block* swap_block;
block_sector_t max_index = 0;
const int sector_per_page=PGSIZE / BLOCK_SECTOR_SIZE;

void swap_init(){
    swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_block != NULL);
    list_init(&swap_slot_list);
}

//store the content of a kpage(frame) to a swap slot(on the disk)
//return an identifier of the swap slot
block_sector_t swap_store(void *kpage) {
    ASSERT(is_kernel_vaddr(kpage));
    block_sector_t index=swap_get_swap_slot();
    if(index==(block_sector_t)(-1)){
        return -1;
    }
    for(int i=0;i<sector_per_page;i++){
        block_write(swap_block,index+i,kpage+i*BLOCK_SECTOR_SIZE);
    }
    return index;
}

//load a swap slot to the kpage(frame)
//index must be got from swap_store()
void swap_load(block_sector_t index, void *kpage) {
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT((int)index>=0 && index % sector_per_page == 0);
    block_sector_t index=swap_get_swap_slot();
    for(int i=0;i<sector_per_page;i++){
        block_read(swap_block,index+i,kpage+i*BLOCK_SECTOR_SIZE);
    }
    swap_free_swap_slot(index);
}

//free a swap slot whose identifier is index
//index must be got from swap_store()
void swap_free_swap_slot(block_sector_t index){
    ASSERT(index % sector_per_page == 0);
    if (index == max_index - sector_per_page){
        max_index -= sector_per_page;
    }else{
        struct swap_slot* slot= malloc(sizeof(struct swap_slot));
        slot->index = index;
        list_push_back(&swap_slot_list, &slot->le);
    }
}

block_sector_t swap_get_swap_slot(){
    if (!list_empty(&swap_slot_list)){
        struct swap_item* slot = list_entry(list_pop_front(&swap_slot_list), struct swap_slot, le);
        int index=t;
        free(t);
        return index;
    }else{
        if (max_index + sector_per_page < block_size(swap_block)){
            max_index += sector_per_page;
            return max_index - sector_per_page;
        }
    }
    return (block_sector_t)-1;
}