#include <bitmap.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"

static struct block *swap_block;
static struct bitmap *swap_available;

static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

// The number of possible swapped pages.
static size_t swap_size;

void swap_init() {
  ASSERT (SECTORS_PER_PAGE > 0);

  swap_block = block_get_role(BLOCK_SWAP);
  if (swap_block == NULL) {
    PANIC ("Error: Can't initialize swap block");
    NOT_REACHED ();
  }

  swap_size = block_size(swap_block) / SECTORS_PER_PAGE;
  swap_available = bitmap_create(swap_size);
  bitmap_set_all(swap_available, true);
}


block_sector_t swap_out(void *page) {
  ASSERT (page >= PHYS_BASE);

  // Find an available block region to use
  size_t swap_index = bitmap_scan(swap_available, 0, 1, true);

  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    block_write(swap_block,
        swap_index * SECTORS_PER_PAGE + i, // Sector number
        page + (BLOCK_SECTOR_SIZE * i) // Target address
        );
  }

  // Occupy the slot, available becomes false
  bitmap_set(swap_available, swap_index, false);
  return swap_index;
}


void swap_in(block_sector_t swap_index, void *page) {
  ASSERT (page >= PHYS_BASE);

  ASSERT (swap_index < swap_size);
  if (bitmap_test(swap_available, swap_index) == true) {
    PANIC ("Error, invalid read access to unassigned swap block");
  }

  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    block_read (swap_block,
        swap_index * SECTORS_PER_PAGE + i, // Sector number
        page + (BLOCK_SECTOR_SIZE * i) // Target address
        );
  }

  bitmap_set(swap_available, swap_index, true);
}

void swap_free(block_sector_t swap_index) {
  ASSERT (swap_index < swap_size);
  if (bitmap_test(swap_available, swap_index) == true) {
    PANIC ("Error, invalid free request to unassigned swap block");
  }
  bitmap_set(swap_available, swap_index, true);
}