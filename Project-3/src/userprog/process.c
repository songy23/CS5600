#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static int retrieve_child_ret (tid_t child_tid);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  tid_t tid;
  char *fn_copy;
  char *fn_copy_2;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  
  /* Make another copy of parsing real file name. */
  fn_copy_2 = palloc_get_page (0);
  
  if (fn_copy_2 == NULL) {
    palloc_free_page (fn_copy);
    return TID_ERROR;
  }
  
  strlcpy (fn_copy_2, file_name, PGSIZE);
  
  /*Declare the argument name
    SAVE_PTR is the address of a `char *' variable used to keep
    track of the tokenizer's position */
  
  char *save_ptr;
  char *real_file_name;
  
  //Use strtok_r to get file name
  real_file_name = strtok_r (fn_copy_2, " ", &save_ptr);
  struct file *file = filesys_open(real_file_name);
  
  if (file == NULL) {
    palloc_free_page (fn_copy);
    palloc_free_page (fn_copy_2);
    return TID_ERROR;
  }
  
  /* Create a new thread to execute FILE_NAME. */
  
  tid = thread_create (real_file_name, PRI_DEFAULT, start_process, fn_copy);
  palloc_free_page (fn_copy_2);
  
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
  else 
    // Block the caller (parent) thread to enforce synchronization
    sema_down (&thread_current ()->sema_exec);
  return tid;
}
  
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  char *token;
  bool success;
  int arguments_length = strlen (file_name) + 1;
  struct thread *cur = thread_current ();
  char *remaining;
  
  /* Token gains the filename value */
  char *exec_file_name = strtok_r (file_name, " ", &remaining);
  
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  success = load (exec_file_name, &if_.eip, &if_.esp);
  if (!success) {
    /* If load failed, quit. */
    palloc_free_page (file_name);
    cur->return_value = -1;
    printf ("%s: exit(%d)\n", cur->name, -1);
    sema_up (&cur->parent->sema_exec);
    thread_exit ();
  }

  // when load() complete, unblock parent thread
  sema_up (&cur->parent->sema_exec);
  struct file *file = filesys_open (exec_file_name);
  file_deny_write (file);
  struct list *fd_list = &(cur->fd_list);
  // create a new fd list node and add it to the fd list of current thread
  struct fd_list_node *new_fd_node =
  (struct fd_list_node *) malloc (sizeof (struct fd_list_node));
  new_fd_node->file = file;
  new_fd_node->fd = cur->current_fd;
  cur->current_fd++;
  list_push_back (fd_list, &new_fd_node->elem);

  /* Create another array for arguments */
  char **arguments = (char **) malloc (arguments_length * sizeof(char));
  /* Tokenise remaining arguments */
  int argc = 0;
  arguments[argc++] = exec_file_name;
  /* Due to carlessness of the user we can have multiple
  consecutive spaces which would break word alignment formula.
  Therefore we need to calculate the actual lenght of arguments*/
  arguments_length = strlen (exec_file_name) + 1;
  while((token = strtok_r(NULL, " ", &remaining))) {
    arguments_length += strlen (token) + 1;
    arguments[argc++] = token;
  }
  
  int **addrs = (int **) malloc (argc * sizeof(int *));
  int i;
  for (i = argc - 1; i >= 0; i--) {
    int arg_length = strlen(arguments[i])+1;
    if_.esp -= arg_length;
    addrs[i] = if_.esp;
    memcpy (if_.esp, arguments[i], arg_length);
  }
  
  if (arguments_length % 4 == 0) {
    if_.esp -= 0;
  } else {
    if_.esp -=4 - arguments_length % 4;
  }

  /* Push 0 sentinel argument. */
  if_.esp -= 4;
  *(int *) if_.esp = 0;
  for (i = argc - 1; i >= 0; i--) {
    if_.esp -= 4;
    *(void **) (if_.esp) = addrs[i];
  }
  /* adding argv */
  if_.esp -= 4;
  *(char **) if_.esp = if_.esp + 4;
  /* adding argc */
  if_.esp -= 4;
  *(int *) if_.esp = argc;
  /* adding fake return address */
  if_.esp -= 4;
  *(int *) if_.esp = 0;
  
  /* Deallocate memory */
  free (addrs);
  free (arguments);
  palloc_free_page (file_name);
  /* Start the user process by simulating a return from an
  interrupt, implemented by intr_exit (in
  threads/intr-stubs.S). Because intr_exit takes all of its
  arguments on the stack in the form of a `struct intr_frame',
  we just point the stack pointer (%esp) to our stack frame
  and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}



/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */

int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct thread *child = NULL;
  struct list_elem *e;
  int return_value = -1;
  
  // Find if the given child_tid exists in the alive child_list
  for (e = list_begin (&cur->child_list); 
       e != list_end (&cur->child_list);
       e = list_next (e)) {
    struct thread *temp = list_entry (e, struct thread, child_elem);
    if (temp->tid == child_tid) {
      child = temp;
      break;
    }
  }
  
  if (child == NULL) {
    return_value = retrieve_child_ret (child_tid);
  } else if (child != NULL && child->being_waited) {
    // cannot be waited twice
    return_value = -1;
  } else {
    child->being_waited = true;
    sema_down (&(child->parent->sema_wait));  // block this thread
    
    // this will happen after the child thread exits
    return_value = retrieve_child_ret (child_tid);  
  }
  
  return return_value;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      // Write back all the dirty pages to disk.
      while (!list_empty (&(cur->sup_page_table))) {
        struct supplemental_page *page = list_entry
        (list_pop_front (&(cur->sup_page_table)), struct supplemental_page, elem);
        if (pagedir_is_dirty (pd, page->upage)) 
          file_write_at (page->file, page->upage, PGSIZE, page->offset);
        // remove_frame (pagedir_get_page (pd, page->upage));
        free (page);
      }
      
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
      
      // Free all the fd_list_node, close all files and allow write.
      while (!list_empty (&(cur->fd_list))) {
        struct fd_list_node *node = list_entry 
             (list_pop_front(&(cur->fd_list)), struct fd_list_node, elem);
        file_allow_write (node->file);
        file_close (node->file);
        free (node);
      }
      
      // Store the return value to its parent's child_ret_list for future use.
      if (cur->parent != NULL) {
        struct child_return_val *ret = (struct child_return_val*) 
                   malloc (sizeof (struct child_return_val));
        ret->tid = cur->tid;
        ret->return_value = cur->return_value;
        list_push_back (&(cur->parent->child_ret_list), &(ret->elem));
        list_remove (&cur->child_elem);
      }
      
      // Wake up (unblock) parent thread.
      if (cur->being_waited && cur->parent != NULL) {
        while (!list_empty (&(cur->parent->sema_wait.waiters)))
          sema_up (&(cur->parent->sema_wait));
      }
      
      // Free all the child_return_val of this thread.
      while (!list_empty (&cur->child_ret_list)) {
        struct child_return_val *child_ret = list_entry 
        (list_pop_front (&cur->child_ret_list), struct child_return_val, elem);
        free (child_ret);
      }
      
      // If this thread has alive children, they all become orphans.
      while (!list_empty (&cur->child_list)) {
        struct thread *child = list_entry
        (list_pop_front (&cur->child_list), struct thread, child_elem);
        child->parent = NULL;
      }
      
      /* Print Process Termination Messages of user programs.
         Do not print these messages when a kernel thread that is not a
         user process terminates */
      printf("%s: exit(%d)\n", cur->name, cur->return_value);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  uint32_t page_index = 0;  // lazy loading, page by page
  struct thread *cur = thread_current();
  lock_acquire (&(cur->sp_table_lock));
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Store pages in supplemental page table for lazy loading,
         those pages will be loaded later when a page fault happpens. */
      struct supplemental_page *new_sppl_page = 
        (struct supplemental_page*) malloc (sizeof (struct supplemental_page));
      new_sppl_page->upage = upage;
      new_sppl_page->file = file;
      new_sppl_page->writable = writable;
      new_sppl_page->read_bytes = page_read_bytes;
      new_sppl_page->zero_bytes = page_zero_bytes;
      new_sppl_page->offset = (uint32_t)ofs + (page_index * (uint32_t)PGSIZE);
      new_sppl_page->mapid = 0;
      new_sppl_page->swap_index = -1;
      new_sppl_page->loaded = false;
      page_index++;
      list_push_back (&(cur->sup_page_table), &(new_sppl_page->elem));

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  lock_release (&(cur->sp_table_lock));
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  uint8_t *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
  bool success = false;

  /* The first stack page needs not be allocated lazily. */
  struct frame *new_frame = get_new_frame (PAL_USER | PAL_ZERO, upage);
  if (new_frame != NULL) 
    {
      kpage = new_frame->kpage;
      success = install_page (upage, kpage, true);
      if (success) {
        *esp = PHYS_BASE;
        thread_current ()->esp = upage;  /* Save esp. */
      } else {
        remove_frame (kpage);
        PANIC ("Fail to install page");
      }
    }
  new_frame->pinned = false;
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Retrieve the return value from child_ret_list with the given child_tid. */
static int retrieve_child_ret (tid_t child_tid)
{
  struct list_elem *e;
  struct thread *cur = thread_current ();
  int return_value = -1;
  for (e = list_begin (&cur->child_ret_list); 
       e != list_end (&cur->child_ret_list);
       e = list_next (e)) {
     struct child_return_val *temp =
             list_entry (e, struct child_return_val, elem);
     if (temp->tid == child_tid) {
       return_value = temp->return_value;
       list_remove (e);
       free (temp);
       break;
     }
  }
  
  return return_value;
}