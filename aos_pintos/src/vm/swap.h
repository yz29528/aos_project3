//
// Created by Administrator on 2024/4/4.
//

#ifndef VM_SWAP_H
#define VM_SWAP_H
#include "devices/block.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"

struct swap_slot{
    block_sector_t index;
    struct list_elem le;
};

//initialize swap when kernel starts
//used in thread/init.c
void swap_init();

//store the content of a kpage(frame) to a swap slot(on the disk)
//return an identifier of the swap slot
block_sector_t swap_store(void *kpage);

//load a swap slot to the kpage(frame)
//index must be got from swap_store()
void swap_load(block_sector_t index, void *kpage);

void swap_free_swap_slot(block_sector_t index);
block_sector_t swap_get_swap_slot();


#endif //AOS_PROJECT3_SWAP_H
