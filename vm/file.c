/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static bool lazy_load_file (struct page *page, struct lazy_file *aux); /* team 7 */

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
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
    struct lazy_file *aux = page->uninit.aux;
    
    file_page->page_cnt = aux->page_cnt;
    file_page->ori_file = aux->ori_file;
    file_page->re_file = aux->re_file;
    file_page->ofs = aux->ofs;
    file_page->page_read_bytes = aux->page_read_bytes;

    return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *curr = thread_current();

    if (pml4_is_dirty(curr->pml4, page->va))
        file_write(file_page->ori_file, page->frame->kva, file_page->page_read_bytes);
    
    if (file_page->page_cnt == 0)
        close(file_page->re_file);
    else 
        file_page->page_cnt--;

    list_remove(&page->frame->f_elem);
    free(page->frame);
}

/* Do the mmap */
/* team 7 */
void *
do_mmap (void *addr, size_t length, int writable, struct file *ori_file, off_t offset) {
    // fail cases
    if (!addr || !length || ori_file < 3 || pg_ofs(addr) || !file_length(ori_file)) 
        goto err;
    
    // reopen
    struct file *file = file_reopen(ori_file);

    struct thread *curr = thread_current();
    struct page *page = NULL;
    size_t page_cnt = (int)(length / PGSIZE); /* debugging ddalgui*/
    if(length % PGSIZE) 
        page_cnt++;
    
    for (int i = 0; i < page_cnt; i++) {
        page = spt_find_page(&curr->spt, (addr + (PGSIZE * i)));
        if(page && page->frame)
            goto err;
    }

    // do mmap
    for (int i = 0; i < page_cnt; i++) {
        struct lazy_file *aux = calloc(1, sizeof(struct lazy_file));
        file->map_count++;
        aux->ori_file = ori_file;
        aux->re_file = file;
        aux->ofs = offset;
        aux->page_read_bytes = length < PGSIZE ? length : PGSIZE;
        aux->page_cnt = page_cnt; 

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

    // fail case
    if (!page->frame || page->operations->type != VM_FILE)
        goto err;

    struct lazy_file *aux = page->uninit.aux;
    for (int i = 0; i < aux->page_cnt; i++) {
        page = spt_find_page(&curr->spt, addr + (i * PGSIZE));
        if (!page)
            goto err;
        // else
        //     file_backed_destroy(page);
    }

err:
    return;
}

bool
lazy_load_file(struct page *page, struct lazy_file *aux) {
    struct file *file = aux->re_file;
    file_seek(file, aux->ofs);

    if (file_read(file, page->frame->kva, aux->page_read_bytes) != aux->page_read_bytes)
        return false;
    
    free(aux);
    return true;
}
