#include "vm/swap.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/synch.h"

static struct bitmap *swap_table;
static struct block *swap_block;
static struct lock swap_table_lock;

/* Initialize the swap table.
   If fail, panic the kernel. */
void 
swap_table_init () 
{
	swap_block = block_get_role (BLOCK_SWAP);
	if (swap_block == NULL)
	  PANIC ("Fail to allocate swap block");
	block_sector_t blk_size = block_size (swap_block);
	// one sector is 512KB, one swap block is 4096KB so it has 8 sectors
	int slot_size = blk_size / 8;  
	swap_table = bitmap_create (slot_size); 
	if (swap_table == NULL)
	  PANIC ("Fail to allocate swap table");
	bitmap_set_all (swap_table, false);
	lock_init (&swap_table_lock);
}

/* Given a kernel page address (which corresponds to a frame)
   Swap a frame to swap disk and return the start index of it in swap table.
   If swap table is full, panic the kernel. */
size_t 
swap_to_disk (uint8_t *kpage)
{
	if (!lock_held_by_current_thread (&swap_table_lock))
	  lock_acquire (&swap_table_lock);
	size_t index = bitmap_scan_and_flip (swap_table, 0, 1, false);
	if (index == BITMAP_ERROR)
	  PANIC ("Swap table is full");
	int i = 0;
	for (i = 0; i < 8; i++) 
	  block_write (swap_block, index * 8 + i, kpage + i * BLOCK_SECTOR_SIZE);
	lock_release (&swap_table_lock);
	return index;
}

/* Swap a page back to frame. */
void
swap_back_to_frame (int slot_index, uint8_t *kpage)
{
	if (!lock_held_by_current_thread (&swap_table_lock))
	  lock_acquire (&swap_table_lock);
	ASSERT (bitmap_test (swap_table, slot_index) == true);
	bitmap_flip (swap_table, slot_index);
	int i = 0;
	for (i = 0; i < 8; i++)
	  block_read (swap_block, slot_index * 8 + i, kpage + i * BLOCK_SECTOR_SIZE);
	lock_release (&swap_table_lock);
}