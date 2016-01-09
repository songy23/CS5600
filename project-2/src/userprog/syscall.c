#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static void sys_halt (struct intr_frame *f);
static void sys_exit (struct intr_frame *f);
static void sys_exec (struct intr_frame *f);
static void sys_wait (struct intr_frame *f);
static void sys_create (struct intr_frame *f);
static void sys_remove (struct intr_frame *f);
static void sys_open (struct intr_frame *f);
static void sys_filesize (struct intr_frame *f);
static void sys_read (struct intr_frame *f);
static void sys_write (struct intr_frame *f);
static void sys_seek (struct intr_frame *f);
static void sys_tell (struct intr_frame *f);
static void sys_close (struct intr_frame *f);
static void exit (int return_value);
static struct fd_list_node* find_fd_in_list (struct list *fd_list, int fd);
static void validate_pointer (const void *address);
static void validate_buffer (void *p, unsigned size);
static void validate_string (void *p);

struct lock filesys_lock;  // used for synchronized read/write

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_pointer (f->esp);
  int syscall_type = *((int*) f->esp);
  switch (syscall_type) {
    case SYS_HALT: {            /* Halt the operating system. */
      sys_halt (f);
      break;
    }                   
    case SYS_EXIT: {            /* Terminate this process. */
      sys_exit (f);
      break;
    }                   
    case SYS_EXEC: {            /* Start another process. */
      sys_exec (f);
      break;
    }                   
    case SYS_WAIT: {            /* Wait for a child process to die. */
      sys_wait (f);
      break;
    }                   
    case SYS_CREATE: {          /* Create a file. */
      sys_create (f);
      break;
    }                 
    case SYS_REMOVE: {          /* Delete a file. */
      sys_remove (f);
      break;
    }                 
    case SYS_OPEN: {            /* Open a file. */
      sys_open (f);
      break;
    }                   
    case SYS_FILESIZE: {        /* Obtain a file's size. */
      sys_filesize (f);
      break;
    }               
    case SYS_READ: {            /* Read from a file. */
      sys_read (f);
      break;
    }                   
    case SYS_WRITE: {           /* Write to a file. */
      sys_write (f);
      break;
    }                  
    case SYS_SEEK: {            /* Change position in a file. */
      sys_seek (f);
      break;
    }                   
    case SYS_TELL: {            /* Report current position in a file. */
      sys_tell (f);
      break;
    }                   
    case SYS_CLOSE: {           /* Close a file. */
      sys_close (f);
      break;
    }
    default: {
      printf ("Syscall %d not supported\n", syscall_type);
    } 
  }
}

/* Halt the operating system. */
static void 
sys_halt (struct intr_frame *f UNUSED)
{
  // syscall0, no argument
  shutdown_power_off ();
}

/* Terminate this process. */
static void 
sys_exit (struct intr_frame *f) 
{
  // syscall1, 1 argument: status
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  int status = *(esp + 1);
  f->eax = 0;
  exit (status);
}

/* Terminate the process and set its return value as the 
   input value. */
static void
exit (int return_value) {
  struct thread *cur = thread_current ();
  cur->return_value = return_value;
  thread_exit ();
}

/* Start another process. */
static void 
sys_exec (struct intr_frame *f)
{
  // syscall1, 1 argument: file 
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  lock_acquire (&filesys_lock);
  const char *file_name = (char *) *((int *) (esp + 1));
  validate_string (file_name);
  if (file_name == NULL) {
    f->eax = -1;
    return;
  }
  f->eax = process_execute (file_name);
  lock_release (&filesys_lock);
}

/* Wait for a child process to die. */
static void 
sys_wait (struct intr_frame *f)
{
  // syscall1, 1 argument: pid
  int *esp = (int *)f->esp;    // esp address
  validate_pointer (esp + 1);
  tid_t tid = *((int *)(esp + 1));
  if (tid == -1)
    f->eax = -1;
  else
    f->eax = process_wait (tid);
}

/* Create a file. */
static void 
sys_create (struct intr_frame *f)
{
  // syscall2, 2 arguments: file, initial_size
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 2);
  unsigned initial_size = * (esp + 2);
  validate_pointer ((int *)(esp + 1));
  const char *file_name = (char *) *((int *)(esp + 1));
  validate_string (file_name);
  lock_acquire (&filesys_lock);
  if (file_name == NULL) {
    lock_release (&filesys_lock);
    exit (-1);
  } else { 
    f->eax = filesys_create (file_name, initial_size);
    lock_release (&filesys_lock);
  }
}

/* Remove a file. */
static void 
sys_remove (struct intr_frame *f)
{
  // syscall1, 1 argument: file
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  const char *file_name = (char *) *((int *) (esp + 1));
  validate_string (file_name);
  lock_acquire (&filesys_lock);
  if (file_name == NULL) {
    lock_release (&filesys_lock);
    exit (-1);
  } else { 
    f->eax = filesys_remove (file_name);
    lock_release (&filesys_lock);
  }
}

/* Open a file. */
static void 
sys_open (struct intr_frame *f)
{
  // syscall1, 1 argument: file
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  const char *file_name = (char *) *(esp + 1);
  validate_string (file_name);
  if (file_name == NULL) {
    f->eax = -1;
    return;
  }
  lock_acquire (&filesys_lock);
  struct file *file = filesys_open (file_name);
  lock_release (&filesys_lock);
  if (file == NULL) {
    f->eax = -1;
  } else {
    struct thread *cur = thread_current ();
    struct list *fd_list = &(cur->fd_list);
    // create a new fd list node and add it to the fd list of current thread
    struct fd_list_node *new_fd_node = 
                (struct fd_list_node *) malloc (sizeof (struct fd_list_node));
    new_fd_node->file = file;
    new_fd_node->fd = cur->current_fd;
    cur->current_fd++;
    list_push_back (fd_list, &new_fd_node->elem);
    f->eax = new_fd_node->fd;
  }
}

/* Obtains a file's size */
static void 
sys_filesize (struct intr_frame *f)
{
  // syscall1, 1 argument: fd
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  int fd = *(esp + 1);         // file descriptor
  lock_acquire (&filesys_lock);
  struct list *fd_list = &(thread_current ()->fd_list);
  struct fd_list_node *node = find_fd_in_list (fd_list, fd);
  if (node != NULL) 
    f->eax = file_length (node->file);
  else 
    f->eax = -1;  // fail to find the target file
  lock_release (&filesys_lock);
}

/* Read from a file. */
static void 
sys_read (struct intr_frame *f)
{
  // syscall3, 3 arguments: fd, buffer, length
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  validate_pointer (esp + 3);
  int fd = *(esp + 1);         // file descriptor
  unsigned length = *(esp + 3);  // size of output
  validate_buffer ((char *) *(esp + 2), length);
  char *buffer = (char *) *(esp + 2);  // buffer
  
  lock_acquire (&filesys_lock);
  if (length <= 0) {  // handle exception
    f->eax = 0;
    lock_release (&filesys_lock);
    return;
  }
  
  if (fd == STDIN_FILENO) {  // read from console
    unsigned int i;  // user unsigned int because length is unsigned
    for (i = 0; i < length; i++)
      // read char from console and store them in buffer
      buffer[i] = input_getc();
    f->eax = length;  // eax = length
  } else {                    // read from a file
    struct list* fd_list = &(thread_current ()->fd_list);
    struct fd_list_node *node = find_fd_in_list (fd_list, fd);
    if (node != NULL)
      f->eax = file_read (node->file, buffer, length);
      // file_read will return the number of bytes actually read,
      // which may be less than the pass-in length.
    else
      f->eax = -1;  // fail to find the file to read
  }
  lock_release (&filesys_lock);
}

/* Write to a file or to console. */
static void 
sys_write (struct intr_frame *f)
{
  // syscall3, 3 arguments: fd, buffer, length
  int *esp = (int *)f->esp;    // esp address
  validate_pointer (esp + 1);
  validate_pointer (esp + 3);
  int fd = *(esp + 1);         // file descriptor
  unsigned length = *(esp + 3);  // size of output
  validate_buffer ((char *) *(esp + 2), length);
  char *buffer = (char *) *(esp + 2);  // buffer
  
  lock_acquire (&filesys_lock);
  if (length <= 0) {  // handle exception
    f->eax = 0;
    lock_release (&filesys_lock);
    return;
  }
  
  if (fd == STDOUT_FILENO) {  // write to console
    putbuf (buffer, length);  
    f->eax = length;  // eax = length
  } else {                    // write to a file
    struct list* fd_list = &(thread_current ()->fd_list);
    struct fd_list_node *node = find_fd_in_list (fd_list, fd);
    if (node != NULL)
      f->eax = file_write (node->file, buffer, length);
      // file_write will return the number of bytes actually written,
      // which may be less than the pass-in length.
    else
      f->eax = -1;  // fail to find the file to write
  }
  lock_release (&filesys_lock);
}

/* Change position in a file. */
static void 
sys_seek (struct intr_frame *f)
{
  // syscall2, 2 arguments: fd, position
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  validate_pointer (esp + 2);
  int fd = *(esp + 1);         // file descriptor
  unsigned position = * (esp + 2);
  struct list* fd_list = &(thread_current ()->fd_list);
  struct fd_list_node *node = find_fd_in_list (fd_list, fd);
  if (node != NULL)
    file_seek (node->file, position);
}

/* Report current position in a file. */
static void 
sys_tell (struct intr_frame *f)
{
  // syscall1, 1 argument: fd
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  int fd = *(esp + 1);         // file descriptor
  struct list* fd_list = &(thread_current ()->fd_list);
  struct fd_list_node *node = find_fd_in_list (fd_list, fd);
  if (node != NULL)
    f->eax = file_tell (node->file);
  else 
    f->eax = -1;
}

/* Close a file. */
static void 
sys_close (struct intr_frame *f)
{
  // syscall1, 1 argument: fd
  int *esp = (int *)f->esp;    // esp address 
  validate_pointer (esp + 1);
  int fd = *(esp + 1);         // file descriptor
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);
  struct list *fd_list = &(cur->fd_list);
  struct list_elem *e;
  for (e = list_begin (fd_list); e != list_end (fd_list); e = list_next (e)) {
    struct fd_list_node *node = list_entry (e, struct fd_list_node, elem);
    if (fd == -1) {  // close all files
      list_remove (&node->elem);
      file_close (node->file);
      free (node);
    } else if (node->fd == fd) {  // close one file
      list_remove (&node->elem);
      if (fd == cur->current_fd - 1)
        cur->current_fd--;  
      // reuse the largest fd value, but if fd of the file to be closed != 
      // current_fd - 1, that fd value will not be reused for later open files
      file_close (node->file);
      free (node);
      break;
    }
  }
  f->eax = 0;
  if (fd == -1)
    cur->current_fd = 0;  // reset the fd value of current thread
  lock_release (&filesys_lock);
}

/* Gievn a file descriptor and a list of fd_node, 
   return the node that matches the input fd if it exists, 
   or NULL otherwise */
static struct fd_list_node* 
find_fd_in_list (struct list *fd_list, int fd) {
  struct list_elem *e;
  struct fd_list_node *node;
  for (e = list_begin (fd_list); e != list_end (fd_list); e = list_next (e)) {
    node = list_entry (e, struct fd_list_node, elem);
    if (node->fd == fd) 
      return node;
  }
  return NULL;
}

/* If the given pointer is not a valid user pointer or points to unmapped
   memory, exit with return value -1. Othrewise do nothing. */
static void
validate_pointer (const void *address) {
  if (!is_user_vaddr (address) || address < 0x08048000 ||
      !pagedir_get_page (thread_current ()->pagedir, address))
    exit (-1);
}

/* If the given buffer contains invalid pointer, exit with return value -1
   Othrewise do nothing. */
static void
validate_buffer (void* p, unsigned size) {
  char *ptr = p;
  unsigned i;
  for(i=0; i<size; i++){
    validate_pointer ((void*)ptr);
    ptr++;
  }
}

/* If the given string addresses contain invalid pointer, 
   exit with return value -1. Othrewise do nothing. */
static void
validate_string (void* p) {
  char *ptr = p;
  char c = ' ';
  int i = 0;
  while (c != '\0') {
    validate_pointer (ptr);
    c = *(char*)ptr;
    ptr++;
  }
}