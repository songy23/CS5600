#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/synch.h"

struct frame {
	struct thread *owner;  /* Owner thread. */
	uint8_t *upage;  /* User page (virtual address). */
	uint8_t *kpage;  /* Kernel page (kernel virtual address). */
	bool pinned;     /* Indicate this frame should not be evicted. */
	
	/* Use a list to implement frame table. */
	struct list_elem elem;
};

/* Initialize the frame table. */
void frame_table_init (void);
/* Look up the given physical address in frame table. */
struct frame * find_frame (uint8_t*);
/* Remove the frame with the given physical address in frame table. */
void remove_frame (uint8_t*);
/* Get a new frame. */
struct frame * get_new_frame (enum palloc_flags, uint8_t*);
/* Evict a frame. */
void evict_frame (void);

#endif /* vm/frame.h */