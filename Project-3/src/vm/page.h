#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "filesys/file.h"
#include "threads/thread.h"

struct supplemental_page 
{
	uint8_t *upage;
	bool loaded;  /* Indicate whether this page is loaded. */
	
	/* Stored information for lazy loading. */
	struct file* file;
	off_t offset;
	bool writable;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	
	/* Track mapid. */
	int mapid;
	
	/* Index of swap slot, initialliy -1. */
	size_t swap_index;
	
	/* Use a list to implement supplemental page table. */
	struct list_elem elem;
};

/* Find the supplemental page that contains the given upage. */
struct supplemental_page* find_page (uint8_t *, struct thread *);
/* Remove the supplemental page that contains the given upage. */
void remove_page (uint8_t *, struct thread *);

#endif /* vm/page.h */