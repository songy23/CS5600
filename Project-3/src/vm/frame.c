#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdbool.h>
#include <list.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

static struct list frame_table;  /* Frame table as a linked list. */
static struct lock frame_table_lock;  /* One lock for synchronization. */

/* Initialize the frame table. */
void 
frame_table_init(void) 
{
	list_init (&frame_table);
	lock_init (&frame_table_lock);
}

struct frame* 
find_frame (uint8_t *kpage) 
{
	struct list_elem *e;
	for (e = list_begin (&frame_table); 
	     e != list_end (&frame_table);
		 e = list_next(e)) {
		struct frame *temp = list_entry (e, struct frame, elem);
		if (temp->kpage == kpage)
		  return temp;	 
	}
	return NULL;
}

void 
remove_frame (uint8_t *kpage) 
{
	if (!lock_held_by_current_thread (&frame_table_lock))
	  lock_acquire (&frame_table_lock);
	struct frame *f = find_frame (kpage);
	if (f != NULL) {
		palloc_free_page (f->kpage);
		list_remove (&f->elem);
		pagedir_clear_page (f->owner->pagedir, pg_round_down (f->upage));
		free (f);
	}
	lock_release (&frame_table_lock);
}

//> typo: Alloctate
/* Alloctate a new frame. Must use PAL_USER to avoid allocating from 
   the "kernel pool". */
struct frame *
get_new_frame (enum palloc_flags flag, uint8_t *upage) 
{
	ASSERT (flag == PAL_USER || flag == PAL_ZERO | PAL_USER);
	if (!lock_held_by_current_thread (&frame_table_lock))
	  lock_acquire (&frame_table_lock);
	uint8_t *kpage_new = palloc_get_page (flag);
	if (kpage_new == NULL) {
		// Evict a frame and re-allocate
		// printf ("Evicting \n");
		evict_frame ();
		kpage_new = palloc_get_page (flag);
	}
	ASSERT (kpage_new != NULL);
	struct frame *frame_new = (struct frame*) malloc (sizeof (struct frame));
	frame_new->kpage = kpage_new;
	frame_new->upage = upage;
	frame_new->owner = thread_current();
	frame_new->pinned = true;
	list_push_back (&frame_table, &frame_new->elem);
	lock_release (&frame_table_lock);
	return frame_new;
}

/* Evict a frame using clock algorithm. */
void 
evict_frame () 
{
	struct list_elem *e = list_begin (&frame_table);
	struct thread *cur = thread_current ();
	while (true) {
		// clock algorithm
		struct frame *f = list_entry (e, struct frame, elem);
		ASSERT (f != NULL);
		struct thread *owner = f->owner;
		
		if (e == list_end (&frame_table))
		  e = list_begin (&frame_table);  // go circular
		else
		  e = list_next (e);
		
		// printf ("upage : %p kpage : %p\n", f->upage, f->kpage);
		if (owner->pagedir == NULL || f->pinned || !is_user_vaddr (f->upage))
		  continue;  // skip pinned frames, or Code or BSS pages
		
		if (!lock_held_by_current_thread (&owner->sp_table_lock))
			  lock_acquire (&(owner->sp_table_lock));
		if (pagedir_is_accessed (owner->pagedir, f->upage)) {
			pagedir_set_accessed (owner->pagedir, f->upage, false);
		} else {
			struct supplemental_page *page = find_page (f->upage, owner);
			ASSERT (page != NULL);
			if (pagedir_is_dirty (owner->pagedir, page->upage)) {
				if (page->mapid == 0)
				  page->swap_index = swap_to_disk (f->kpage);
				else 
				  file_write_at 
				     (page->file, page->upage, page->read_bytes, page->offset);
			}
		    page->loaded = false;
		    pagedir_clear_page (owner->pagedir, pg_round_down (f->upage));
			palloc_free_page (f->kpage);
			list_remove (&f->elem);
			free (f);
			break;
		}
		lock_release (&(owner->sp_table_lock));
	}
}
