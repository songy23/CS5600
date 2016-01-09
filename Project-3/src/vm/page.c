#include "vm/page.h"
#include <list.h>
#include "threads/thread.h"

/* Find the supplemental page that contains the given upage. */
struct supplemental_page*
find_page (uint8_t *upage, struct thread *t) 
{
	struct list_elem *e;
	struct list *sppl_table = &(t->sup_page_table);
	struct supplemental_page *target = NULL;
	struct thread *cur = thread_current ();
	if (!lock_held_by_current_thread (&cur->sp_table_lock))
      lock_acquire (&(cur->sp_table_lock));
	for (e = list_begin (sppl_table); 
	     e != list_end (sppl_table); 
		 e = list_next(e)) {
			struct supplemental_page *temp = 
			       list_entry (e, struct supplemental_page, elem);
		    if (temp->upage == upage) {
				target = temp;
				break;
			}
		 }
    if (lock_held_by_current_thread (&cur->sp_table_lock))
	  lock_release (&(cur->sp_table_lock));	 
	return target;
}

/* Remove the supplemental page that contains the given upage. */
void 
remove_page (uint8_t *upage, struct thread *t) 
{
	struct list_elem *e;
	struct list *sppl_table = &(t->sup_page_table);
	struct thread *cur = thread_current ();
	if (!lock_held_by_current_thread (&cur->sp_table_lock))
	  lock_acquire (&(cur->sp_table_lock));
	for (e = list_begin (sppl_table); 
	     e != list_end (sppl_table); 
		 e = list_next(e)) {
			struct supplemental_page *temp = 
			       list_entry (e, struct supplemental_page, elem);
		    if (temp->upage == upage) {
				list_remove (e);
				free (temp);
				break;
			}
		 }
    if (lock_held_by_current_thread (&cur->sp_table_lock))
	  lock_release (&(cur->sp_table_lock));
}
