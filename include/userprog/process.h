#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* get child */
struct thread * get_child(int pid);

/*** team 7 : for lazy load ***/
struct lazy_aux {
    // file은 current thread의 running file로
    size_t page_read_bytes;
    size_t page_zero_bytes;
    off_t ofs; // file_seek offset
    struct file *file;
};

#endif /* userprog/process.h */
