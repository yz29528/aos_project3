//
// Created by Administrator on 2024/4/4.
//

#ifndef VM_SWAP_H
#define VM_SWAP_H
#include "devices/block.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"


/* Functions for Swap Table manipulation. */

/**
 * Initialize the swap. Must be called ONLY ONCE at the initializtion phase.
 */
void swap_init (void);

/**
 * Write the content of `page` into the swap disk,
 * and return the index of swap region in which it is placed.
 */
block_sector_t swap_out(void *page);

/**
 * Read the content of from the specified swap index,
 * from the mapped swap block, and store PGSIZE bytes into `page`.
 */
void swap_in(block_sector_t swap_index, void *page);

/**
 * Drop the swap region.
 */
void swap_free(block_sector_t swap_index);


#endif //AOS_PROJECT3_SWAP_H
