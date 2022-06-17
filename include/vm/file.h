#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "threads/malloc.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
    struct file *ori_file;
    struct file *re_file;
    off_t ofs;
    size_t page_read_bytes;
    int *page_cnt;
};

struct lazy_file {
    struct file *ori_file;
    struct file *re_file;
    off_t ofs;
    size_t page_read_bytes;
    size_t *page_cnt;
};

struct lock file_lock;
struct lock frame_lock;

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *ori_file, off_t offset);
void do_munmap (void *va);
#endif
