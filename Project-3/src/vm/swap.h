#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/frame.h"
#include "vm/page.h"
#include <bitmap.h>
#include "devices/block.h"

/* Initialize the swap table. */
void swap_table_init (void);
/* Swap a frame to swap disk and return the start index of it in swap table.
   If swap table is full, panic the kernel. */
size_t swap_to_disk (uint8_t *);
/* Swap a page back to frame. */
void swap_back_to_frame (int, uint8_t *);

#endif /* vm/swap.h */