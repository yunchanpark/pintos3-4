/* file.c: Implementation of memory backed file object (mmaped object). */
#include "threads/thread.h"
#include "vm/vm.h"
#include "threads/synch.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
    lock_init(&file_lock);
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
    struct lazy_file *aux = page->uninit.aux;
    
    file_page->page_cnt = aux->page_cnt;
    file_page->re_file = aux->re_file;
    file_page->ofs = aux->ofs;
    file_page->page_read_bytes = aux->page_read_bytes;
    file_page->page_zero_bytes = aux->page_zero_bytes;

    return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
    struct file *file = file_page->re_file;

    if(*(file_page->page_cnt) == 0) {
        lock_acquire(&file_lock);
        file = file_reopen(file_page->re_file);
        lock_release(&file_lock);
    }
    
    lock_acquire(&file_lock);
    file_read_at(file, kva, file_page->page_read_bytes, file_page->ofs);
    lock_release(&file_lock);
    *file_page->page_cnt++;
    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *curr = thread_current();
    if (pml4_is_dirty(curr->pml4, page->va)) {
        lock_acquire(&file_lock);
        file_write_at(file_page->re_file, page->frame->kva, file_page->page_read_bytes, file_page->ofs);
        lock_release(&file_lock);
        pml4_set_dirty(curr->pml4, page->frame->kva, false);
    }
    
    if (*(file_page->page_cnt) != 0)
        *(file_page->page_cnt)--;
    if(*(file_page->page_cnt) == 0) {
        lock_acquire(&file_lock);
        file_close(file_page->re_file);
        lock_release(&file_lock);
    }

    pml4_clear_page(curr->pml4, page->va);
    lock_acquire(&frame_lock);
    list_remove(&page->frame->f_elem);
    palloc_free_page(page->frame->kva);
    lock_release(&frame_lock);

    return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *curr = thread_current();

    if (pml4_is_dirty(curr->pml4, page->va)) {
        lock_acquire(&file_lock);
        file_write_at(file_page->re_file, page->frame->kva, file_page->page_read_bytes, file_page->ofs);
        lock_release(&file_lock);
        pml4_set_dirty(curr->pml4, page->frame->kva, false);
    }
    
    *(file_page->page_cnt)--;
    if(*(file_page->page_cnt) == 0) {
        lock_acquire(&file_lock);
        file_close(file_page->re_file);
        lock_release(&file_lock);
        free(file_page->page_cnt);
    }

    pml4_clear_page(curr->pml4, page->va);
    if (page->frame != NULL) {
        lock_acquire(&frame_lock);
        list_remove(&page->frame->f_elem);
        palloc_free_page(page->frame->kva);
        free(page->frame);
        lock_release(&frame_lock);
    }
}

/* Do the mmap */
/* team 7 */
void *
do_mmap (void *addr, size_t length, int writable, struct file *ori_file, off_t offset) {
    if (addr <= 0 || ori_file < 3 || pg_ofs(addr) || !file_length(ori_file) || is_kernel_vaddr(addr) || (long)length <= 0 || offset % PGSIZE) 
        goto err;
    
    struct file *file = file_reopen(ori_file);

    struct thread *curr = thread_current();
    struct page *page = NULL;
    size_t page_cnt = (int)(length / PGSIZE);
    if(length % PGSIZE) 
        page_cnt++;
    
    for (int i = 0; i < page_cnt; i++) {
        page = spt_find_page(&curr->spt, (addr + (PGSIZE * i)));
        if(page && page->frame)
            goto err;
    }
    int *tmp = calloc(1, sizeof(int *));
    *tmp = page_cnt;

    for (int i = 0; i < page_cnt; i++) {
        struct lazy_file *aux = calloc(1, sizeof(struct lazy_file));
        file->map_count++;
        aux->re_file = file;
        aux->ofs = offset;
        aux->page_read_bytes = length < PGSIZE ? length : PGSIZE;
        aux->page_zero_bytes = PGSIZE - aux->page_read_bytes; 
        aux->page_cnt = tmp;

        if (!vm_alloc_page_with_initializer(VM_FILE, addr + (PGSIZE * i), writable, lazy_load_file, aux))
            goto err;

        length -= aux->page_read_bytes;
        offset += aux->page_read_bytes;
    }

    return addr; 

err :
    return NULL;
}

/* Do the munmap */
void
do_munmap (void *addr) {
    struct thread *curr = thread_current();
    struct page *page = spt_find_page(&curr->spt, addr);
    if(!page)
        goto err;

    if (!page->frame || page->operations->type != VM_FILE)
        goto err;

    for (int i = 0; i < page->file.page_cnt; i++) {
        page = spt_find_page(&curr->spt, addr + (i * PGSIZE));
        if (!page)
            goto err;
        spt_remove_page(&curr->spt, page);
    }

err:
    return;
}

bool
lazy_load_file(struct page *page, struct lazy_file *aux) {
    struct file *file = aux->re_file;
    file_read_at(file, page->frame->kva, aux->page_read_bytes, aux->ofs);
    free(aux);
    return true;
}
